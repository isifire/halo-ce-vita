/*
 * D3D8_VITAGL.C
 *
 * PlayStation Vita hardware Direct3D 8 implementation powered by VitaGL.
 * Implements the Xbox Direct3D 8 entry points, state tables, resource locks,
 * and draw pipelines directly on top of the Vita SGX543MP4+ GPU.
 */

#include <psp2/kernel/processmgr.h>
#include <psp2/display.h>
#include <psp2/ctrl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <vitaGL.h>

#include <xtl.h>
#include "halo_vita_memory.h"
#include "xgpu.h"

/* ---------- State Tables & Global Storage */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][32];
WORD *D3D__IndexData = NULL;

static volatile unsigned int vita_flip_count = 0;
static void (*g_vblank_callback)(unsigned long) = NULL;
volatile unsigned int *d3d_find_flipcount(void) {
    return &vita_flip_count;
}

/* ---------- Device & Surface State */

static int g_dummy_device_data = 1;
static D3DDevice *g_d3d_device = (D3DDevice *)&g_dummy_device_data;
static D3DSurface g_back_buffer;
static D3DSurface g_depth_buffer;
static int g_vitagl_initialized = 0;
static GLint g_ffp_texture_units = 1;
static D3DVIEWPORT8 g_viewport = { 0, 0, 960, 544, 0.0f, 1.0f };

#define VITA_IMMEDIATE_MAX_VERTICES 4096
typedef struct vita_immediate_vertex {
    float x, y, z;
    float u, v;
    unsigned char color[4];
    float attributes[16][4];
} vita_immediate_vertex;

static vita_immediate_vertex *g_immediate_vertices;
static unsigned int g_immediate_capacity;
static unsigned int g_immediate_count;
static enum _D3DPRIMITIVETYPE g_immediate_type;
static int g_immediate_active;
static float g_immediate_attributes[16][4];
static int g_immediate_color_register = D3DVSDE_DIFFUSE;
static int g_immediate_texcoord_register = D3DVSDE_TEXCOORD0;

static GLenum d3d_to_gl_prim(enum _D3DPRIMITIVETYPE pt);
static void vita_apply_render_states(void);
static void vita_apply_vertex_declaration(unsigned int base_vertex);

struct vita_vertex_element { unsigned int stream, offset, type, present; };
struct vita_vertex_shader {
    struct vita_vertex_element elements[16];
    unsigned int instruction_count;
    DWORD *instructions;
    unsigned int identity;
};
static struct vita_vertex_shader *g_vertex_shaders[512];
static struct vita_vertex_shader *g_vertex_declaration;
static struct vita_vertex_shader *g_vertex_program_slots[136];
static unsigned int g_vertex_program_address, g_vertex_shader_identity;
static unsigned long g_shader_constant_mode;
static struct vita_vertex_shader *vita_current_vertex_program(void) {
    return g_vertex_program_slots[g_vertex_program_address];
}

struct d3d_texture_impl {
    struct D3DTexture base;
    unsigned int width;
    unsigned int height;
    unsigned int levels;
    enum _D3DFORMAT format;
    GLuint gl_id;
    int uploaded;
    void *cpu_data;
    unsigned int pitch;
    unsigned int data_size;
    unsigned int level_offsets[13];
    unsigned int level_pitches[13];
    GLuint framebuffer;
    GLuint depthbuffer;
    struct d3d_texture_impl *surface_owner;
    unsigned int surface_level;
    struct d3d_texture_impl *surfaces[13];
};

static struct d3d_texture_impl *g_textures[D3DTSS_MAXSTAGES] = {0};
static struct D3DBaseTexture *g_texture_sources[D3DTSS_MAXSTAGES] = {0};
static struct D3DPalette *g_palettes[D3DTSS_MAXSTAGES] = {0};
static struct d3d_texture_impl *g_render_texture;
static struct d3d_texture_impl *vita_texture_binding_get(struct D3DBaseTexture *texture);
static unsigned long g_texture_modes;
static GLuint g_text_program;
static int g_text_program_attempted;
static GLint g_fragment_texture_units = 1;
static struct D3DVertexBuffer *g_streams[16];
static unsigned int g_stream_strides[16];
static float g_vsh_constants[192][4];
static float g_viewport_scale[4], g_viewport_offset[4];
static int vita_is_format_linear(enum _D3DFORMAT format, unsigned long size);

static void vita_update_viewport_constants(void) {
    unsigned int format = (g_depth_buffer.Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
    float zscale = (format == D3DFMT_D16 || format == D3DFMT_LIN_D16) ? 65535.0f : 16777215.0f;
    g_viewport_scale[0] = g_viewport.Width * 0.5f;
    g_viewport_scale[1] = -(float)g_viewport.Height * 0.5f;
    g_viewport_scale[2] = zscale * (g_viewport.MaxZ - g_viewport.MinZ);
    g_viewport_offset[0] = g_viewport.X + g_viewport.Width * 0.5f;
    g_viewport_offset[1] = g_viewport.Y + g_viewport.Height * 0.5f;
    g_viewport_offset[2] = zscale * g_viewport.MinZ;
    if (!(g_shader_constant_mode & D3DSCM_NORESERVEDCONSTANTS)) {
        memcpy(g_vsh_constants[58], g_viewport_scale, sizeof(g_viewport_scale));
        memcpy(g_vsh_constants[59], g_viewport_offset, sizeof(g_viewport_offset));
    }
}

struct vita_combiner_program {
    struct nv2a_pixel_shader_key key;
    GLuint program;
    unsigned int vertex_identity;
    int attempted;
};
#define VITA_COMBINER_CACHE_SIZE 128
static struct vita_combiner_program g_combiner_programs[VITA_COMBINER_CACHE_SIZE];
static unsigned int g_combiner_program_next;
/* VitaGL's previous global-variable rewrite could search past the end of its
 * shader and crash in strcasestr. The global-scope scanner has been hardened;
 * exercise the real NV2A shader path and retain fixed-function fallback when
 * a shader fails to compile or link. */
static int g_vita_nv2a_combiner_enabled = 1;
static int g_vita_nv2a_combiner_bypass_logged;
static unsigned int vita_texture_mode(unsigned int stage);


static void vita_shader_log(const char *message) {
    FILE *log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "vita_renderer: %s\n", message);
        fclose(log);
    }
}

/* Called by the instrumented VitaGL build around its deferred GLSL/Cg stages.
 * Keep each marker on disk before entering the next stage so a native abort
 * still leaves a useful last checkpoint in boot.log. */
void halo_vita_glsl_checkpoint(const char *message) {
    FILE *log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "vitaGL_trace: %s\n", message ? message : "<null>");
        fclose(log);
    }
}

static int vita_shader_status(GLuint shader, const char *name) {
    GLint status = GL_FALSE;
    char info[1024] = {0};
    FILE *log;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    glGetShaderInfoLog(shader, sizeof(info), NULL, info);
    log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "vita_renderer: %s shader compile=%d log=%s\n", name, (int)status,
            info[0] ? info : "<empty>");
        fclose(log);
    }
    if (!status) {
        return GL_FALSE;
    }
    return GL_TRUE;
}

static void vita_combiner_checkpoint(const char *step, GLuint object) {
    FILE *log = fopen("ux0:data/halo/boot.log", "a");
    GLenum error = glGetError();
    if (log) {
        fprintf(log, "vita_combiner: %s object=%u gl_error=0x%x time_ms=%lu\n",
            step, (unsigned int)object, (unsigned int)error,
            (unsigned long)(sceKernelGetProcessTimeWide() / 1000));
        fclose(log);
    }
}

static float vita_state_float(unsigned int state) {
    float value;
    memcpy(&value, &D3D__RenderState[state], sizeof(value));
    return value;
}

static void vita_color_uniform(GLuint program, const char *name, unsigned int color) {
    GLfloat rgba[4];
    GLint location = glGetUniformLocation(program, name);
    /* Only -1 denotes an inactive uniform in the GL interface. */
    if (location == -1) return;
    rgba[0] = ((color >> 16) & 255) / 255.0f;
    rgba[1] = ((color >> 8) & 255) / 255.0f;
    rgba[2] = (color & 255) / 255.0f;
    rgba[3] = ((color >> 24) & 255) / 255.0f;
    glUniform4fv(location, 1, rgba);
}

static void vita_key_color_uniform(GLuint program, const char *name, const GLfloat value[4]) {
    GLint location = glGetUniformLocation(program, name);
    glUniform4fv(location, 1, value);
}

static void vita_pixel_key(struct nv2a_pixel_shader_key *key) {
    unsigned int stage;
    memset(key, 0, sizeof(*key));
    memcpy(key->combiner_state, D3D__RenderState, sizeof(key->combiner_state));
    /* Constant colors are uniforms, not shader identity. */
    memset(&key->combiner_state[D3DRS_PSCONSTANT0_0], 0, 16 * sizeof(DWORD));
    key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT0] = 0;
    key->combiner_state[D3DRS_PSFINALCOMBINERCONSTANT1] = 0;
    key->texture_modes = g_texture_modes;
    for (stage = 0; stage < 4; ++stage) {
        unsigned int mode = vita_texture_mode(stage);
        key->sampler_type[stage] = mode && mode != 4 &&
            stage < (unsigned int)g_fragment_texture_units && g_textures[stage] && g_textures[stage]->uploaded ?
            _xgpu_sampler_2d : _xgpu_sampler_none;
        key->alpha_kill[stage] = D3D__TextureState[stage][D3DTSS_ALPHAKILL] == D3DTALPHAKILL_ENABLE;
        key->color_sign[stage] = (unsigned char)((D3D__TextureState[stage][D3DTSS_COLORSIGN] >> 28) & 15);
    }
    key->alpha_test_function = D3D__RenderState[D3DRS_ALPHATESTENABLE] ? D3D__RenderState[D3DRS_ALPHAFUNC] : 0;
    key->fog_enable = D3D__RenderState[D3DRS_FOGENABLE] != 0;
    key->fog_table_mode = (unsigned char)D3D__RenderState[D3DRS_FOGTABLEMODE];
}

static GLuint vita_combiner_program_get(struct nv2a_pixel_shader_key *key) {
    unsigned int i, slot;
    struct vita_vertex_shader *vsh = vita_current_vertex_program();
    GLuint vertex, fragment, program;
    GLint linked = GL_FALSE;
    char *source, *vertex_source;
    FILE *log;
    if (!vsh || !vsh->instructions) return 0;
    for (i = 0; i < VITA_COMBINER_CACHE_SIZE; ++i)
        if (g_combiner_programs[i].attempted && g_combiner_programs[i].vertex_identity == vsh->identity &&
            memcmp(&g_combiner_programs[i].key, key, sizeof(*key)) == 0)
            return g_combiner_programs[i].program;

    slot = g_combiner_program_next++ % VITA_COMBINER_CACHE_SIZE;
    if (g_combiner_programs[slot].program) glDeleteProgram(g_combiner_programs[slot].program);
    g_combiner_programs[slot].key = *key;
    g_combiner_programs[slot].vertex_identity = vsh->identity;
    g_combiner_programs[slot].program = 0;
    g_combiner_programs[slot].attempted = 1;
    source = nv2a_pixel_shader_to_glsl(key);
    vertex_source = nv2a_vertex_shader_to_glsl(vsh->instructions, vsh->instruction_count, 0);
    if (!source || !vertex_source) { free(source); free(vertex_source); return 0; }
    /* Keep the exact failing input with the boot log. Shader compile status is
     * deferred by VitaGL; a failure can occur later, inside glLinkProgram. */
    log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "vita_combiner: generated fragment bytes=%lu stages=%lu modes=%08lx\n%s\n",
            (unsigned long)strlen(source),
            key->combiner_state[D3DRS_PSCOMBINERCOUNT] & 0xff,
            key->texture_modes, source);
        fclose(log);
    }
    vita_combiner_checkpoint("create vertex shader begin", 0);
    vertex = glCreateShader(GL_CG_VERTEX_SHADER_EXT);
    vita_combiner_checkpoint("create vertex shader end", vertex);
    if (!vertex) { free(source); free(vertex_source); return 0; }
    fragment = glCreateShader(GL_CG_FRAGMENT_SHADER_EXT);
    vita_combiner_checkpoint("create fragment shader end", fragment);
    if (!fragment) {
        glDeleteShader(vertex);
        free(source);
        free(vertex_source);
        return 0;
    }
    log = fopen("ux0:data/halo/boot.log", "a");
    if (log) { fprintf(log, "vita_combiner: native vertex id=%u instructions=%u\n%s\n", vsh->identity, vsh->instruction_count, vertex_source); fclose(log); }
    glShaderSource(vertex, 1, (const GLchar * const *)&vertex_source, NULL);
    free(vertex_source);
    glShaderSource(fragment, 1, (const GLchar * const *)&source, NULL);
    glCompileShader(vertex);
    vita_combiner_checkpoint("compile vertex shader returned", vertex);
    glCompileShader(fragment);
    vita_combiner_checkpoint("compile fragment shader returned", fragment);
    if (!vita_shader_status(vertex, "NV2A vertex") || !vita_shader_status(fragment, "NV2A fragment")) {
        log = fopen("ux0:data/halo/boot.log", "a");
        if (log) { fprintf(log, "vita_combiner: generated source follows\n%s\n", source); fclose(log); }
        glDeleteShader(vertex); glDeleteShader(fragment); free(source); return 0;
    }
    vita_combiner_checkpoint("create program begin", 0);
    program = glCreateProgram();
    vita_combiner_checkpoint("create program end", program);
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        free(source);
        return 0;
    }
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    vita_combiner_checkpoint("attach shaders returned", program);
    for (i = 0; i < 16; ++i) {
        char attribute[24];
        snprintf(attribute, sizeof(attribute), "v%u_in", i);
        glBindAttribLocation(program, i, attribute);
    }
    vita_combiner_checkpoint("bind attributes returned", program);
    glLinkProgram(program);
    vita_combiner_checkpoint("link program returned", program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    {
        char info[1024] = {0};
        glGetProgramInfoLog(program, sizeof(info), NULL, info);
        log = fopen("ux0:data/halo/boot.log", "a");
        if (log) { fprintf(log, "vita_combiner: link=%d modes=%08lx combiner_count=%lu glsl_log=%s\n",
            (int)linked, key->texture_modes, key->combiner_state[D3DRS_PSCOMBINERCOUNT], info[0] ? info : "<empty>"); fclose(log); }
    }
    glDeleteShader(vertex); glDeleteShader(fragment); free(source);
    if (!linked) { glDeleteProgram(program); return 0; }
    g_combiner_programs[slot].program = program;
    return program;
}

static void vita_combiner_uniforms(GLuint program) {
    unsigned int stage;
    GLfloat values[4][4];
    char name[40];
    for (stage = 0; stage < 8; ++stage) {
        snprintf(name, sizeof(name), "ps_c0[%u]", stage);
        vita_color_uniform(program, name, D3D__RenderState[D3DRS_PSCONSTANT0_0 + stage]);
        snprintf(name, sizeof(name), "ps_c1[%u]", stage);
        vita_color_uniform(program, name, D3D__RenderState[D3DRS_PSCONSTANT1_0 + stage]);
    }
    vita_color_uniform(program, "ps_final_c0", D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0]);
    vita_color_uniform(program, "ps_final_c1", D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1]);
    vita_color_uniform(program, "fog_color", D3D__RenderState[D3DRS_FOGCOLOR]);
    for (stage = 0; stage < 4; ++stage) {
        values[stage][0] = vita_state_float(D3DRS_FOGSTART);
        values[stage][1] = vita_state_float(D3DRS_FOGEND);
        values[stage][2] = vita_state_float(D3DRS_FOGDENSITY);
        values[stage][3] = 0.0f;
        snprintf(name, sizeof(name), "texture_scale[%u]", stage);
        {
            GLfloat scale[4] = {1, 1, 1, 1};
            if (g_textures[stage] && vita_is_format_linear(g_textures[stage]->format, g_textures[stage]->base.Size)) {
                scale[0] = 1.0f / g_textures[stage]->width;
                scale[1] = 1.0f / g_textures[stage]->height;
            }
            vita_key_color_uniform(program, name, scale);
        }
        snprintf(name, sizeof(name), "bump_matrix[%u]", stage);
        {
            GLfloat matrix[4];
            memcpy(matrix, &D3D__TextureState[stage][D3DTSS_BUMPENVMAT00], sizeof(matrix));
            vita_key_color_uniform(program, name, matrix);
        }
        snprintf(name, sizeof(name), "bump_luminance[%u]", stage);
        {
            GLfloat luminance[4] = {0, 0, 0, 0};
            memcpy(luminance, &D3D__TextureState[stage][D3DTSS_BUMPENVLSCALE], 2 * sizeof(float));
            vita_key_color_uniform(program, name, luminance);
        }
    }
    vita_key_color_uniform(program, "fog_parameters", values[0]);
    {
        GLint location = glGetUniformLocation(program, "alpha_reference");
        glUniform1f(location, (GLfloat)(D3D__RenderState[D3DRS_ALPHAREF] & 255));
    }
    for (stage = 0; stage < 4 && stage < (unsigned int)g_fragment_texture_units; ++stage) {
        snprintf(name, sizeof(name), "tex%u", stage);
        {
            GLint location = glGetUniformLocation(program, name);
            glUniform1i(location, (GLint)stage);
        }
        glActiveTexture(GL_TEXTURE0 + stage);
        glBindTexture(GL_TEXTURE_2D, g_textures[stage] ? g_textures[stage]->gl_id : 0);
        if (g_textures[stage]) {
            unsigned int u = D3D__TextureState[stage][D3DTSS_ADDRESSU];
            unsigned int v = D3D__TextureState[stage][D3DTSS_ADDRESSV];
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, u == D3DTADDRESS_WRAP ? GL_REPEAT : (u == D3DTADDRESS_MIRROR ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, v == D3DTADDRESS_WRAP ? GL_REPEAT : (v == D3DTADDRESS_MIRROR ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE));
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, D3D__TextureState[stage][D3DTSS_MINFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, D3D__TextureState[stage][D3DTSS_MAGFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
        }
    }
    glActiveTexture(GL_TEXTURE0);
}

static int vita_needs_combiner(void) {
    struct vita_vertex_shader *vsh = vita_current_vertex_program();
    int required = g_fragment_texture_units >= 4 && vsh && vsh->instructions;
    if (required && !g_vita_nv2a_combiner_enabled) {
        if (!g_vita_nv2a_combiner_bypass_logged) {
            vita_shader_log("NV2A multi-stage combiner disabled: last device log stopped inside deferred VitaGL glLinkProgram; using fixed-function fallback");
            g_vita_nv2a_combiner_bypass_logged = 1;
        }
        return 0;
    }
    return required;
}

static int vita_combiner_vertex_attributes(const unsigned char *base, unsigned int stride,
    unsigned int base_vertex, int immediate, unsigned int vertex_limit) {
    static float *converted[16];
    static unsigned int capacity[16];
    unsigned int reg, vertex;
    (void)base; (void)stride;
    for (reg = 0; reg < 16; ++reg) {
        struct vita_vertex_element *element = g_vertex_declaration ? &g_vertex_declaration->elements[reg] : NULL;
        const unsigned char *data;
        unsigned int type, count, encoding, source_stride;
        GLenum gl_type;
        GLboolean normalized;
        glDisableVertexAttribArray(reg);
        if (immediate) {
            glVertexAttribPointer(reg, 4, GL_FLOAT, GL_FALSE, sizeof(vita_immediate_vertex), g_immediate_vertices[0].attributes[reg]);
            glEnableVertexAttribArray(reg);
            continue;
        }
        if (!element || !element->present || element->stream >= 16 ||
            !g_streams[element->stream] || !g_streams[element->stream]->Data) {
            glVertexAttrib4f(reg, g_immediate_attributes[reg][0], g_immediate_attributes[reg][1],
                g_immediate_attributes[reg][2], g_immediate_attributes[reg][3]);
            continue;
        }
        source_stride = g_stream_strides[element->stream];
        data = (const unsigned char *)g_streams[element->stream]->Data + element->offset + base_vertex * source_stride;
        type = element->type;
        count = type >> 4;
        encoding = type & 15;
        if (type == D3DVSDT_NORMPACKED3 || type == D3DVSDT_D3DCOLOR || type == D3DVSDT_FLOAT2H) {
            if (vertex_limit > capacity[reg]) {
                float *memory = realloc(converted[reg], vertex_limit * 4 * sizeof(float));
                if (!memory) {
                    for (reg = 0; reg < 16; ++reg) glDisableVertexAttribArray(reg);
                    vita_shader_log("attribute conversion allocation failed; draw skipped");
                    return 0;
                }
                converted[reg] = memory;
                capacity[reg] = vertex_limit;
            }
            for (vertex = 0; vertex < vertex_limit; ++vertex) {
                float *out = converted[reg] + vertex * 4;
                const unsigned char *in = data + vertex * source_stride;
                unsigned int packed;
                memcpy(&packed, in, sizeof(packed));
                out[3] = 1.0f;
                if (type == D3DVSDT_NORMPACKED3) {
                    out[0] = (int)(packed << 21) / 2097152 / 1023.0f;
                    out[1] = (int)(packed << 10) / 2097152 / 1023.0f;
                    out[2] = ((int)packed >> 22) / 511.0f;
                } else if (type == D3DVSDT_D3DCOLOR) {
                    out[0] = in[2] / 255.0f; out[1] = in[1] / 255.0f;
                    out[2] = in[0] / 255.0f; out[3] = in[3] / 255.0f;
                } else {
                    memcpy(out, in, 2 * sizeof(float));
                    out[2] = 0.0f;
                    memcpy(out + 3, in + 2 * sizeof(float), sizeof(float));
                }
            }
            glVertexAttribPointer(reg, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), converted[reg]);
        } else {
            if (!count || count > 4) { glVertexAttrib4f(reg, 0, 0, 0, 1); continue; }
            gl_type = encoding == 2 ? GL_FLOAT : ((encoding == 1 || encoding == 5) ? GL_SHORT : GL_UNSIGNED_BYTE);
            normalized = (encoding == 1 || encoding == 4) ? GL_TRUE : GL_FALSE;
            glVertexAttribPointer(reg, count, gl_type, normalized, source_stride, data);
        }
        glEnableVertexAttribArray(reg);
    }
    return 1;
}


static void vita_combiner_matrix(GLuint program, int immediate) {
    unsigned int reg;
    (void)immediate;
    /* Upload only active constants; optimized Cg arrays can be shorter than c[192]. */
    for (reg = 0; reg < 192; ++reg) {
        char name[24];
        snprintf(name, sizeof(name), "c[%u]", reg);
        glUniform4fv(glGetUniformLocation(program, name), 1, g_vsh_constants[reg]);
    }
    vita_key_color_uniform(program, "viewport_scale", g_viewport_scale);
    vita_key_color_uniform(program, "viewport_offset", g_viewport_offset);
    glUniform1f(glGetUniformLocation(program, "point_size"), 1.0f);
    glUniform1f(glGetUniformLocation(program, "screen_offset"), 0.0f);
}

static int vita_draw_combiner(enum _D3DPRIMITIVETYPE primitive_type, unsigned int count,
    unsigned int start_vertex, const unsigned short *indices, const unsigned char *base,
    unsigned int stride, unsigned int base_vertex) {
    struct nv2a_pixel_shader_key key;
    GLuint program;
    unsigned int first, vertex_limit = start_vertex + count;
    if (!vita_needs_combiner()) return 0;
    vita_pixel_key(&key);
    program = vita_combiner_program_get(&key);
    if (!program) return 0;

    {
        static unsigned int trace_count;
        if (trace_count < 48) {
            unsigned int stage;
            FILE *log = fopen("ux0:data/halo/boot.log", "a");
            if (log) {
                fprintf(log, "native_model: draw=%u vs=%u program=%u count=%u modes=%08lx blend=%lu/%lx/%lx op=%lx blendcolor=%08lx fog=%lu/%08lx c0=%08lx c1=%08lx final=%08lx/%08lx\n",
                    trace_count, vita_current_vertex_program()->identity, program, count, g_texture_modes,
                    D3D__RenderState[D3DRS_ALPHABLENDENABLE], D3D__RenderState[D3DRS_SRCBLEND], D3D__RenderState[D3DRS_DESTBLEND], D3D__RenderState[D3DRS_BLENDOP],
                    D3D__RenderState[D3DRS_BLENDCOLOR],
                    D3D__RenderState[D3DRS_FOGENABLE], D3D__RenderState[D3DRS_FOGCOLOR],
                    D3D__RenderState[D3DRS_PSCONSTANT0_0], D3D__RenderState[D3DRS_PSCONSTANT1_0],
                    D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0], D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1]);
                for (stage = 0; stage < 4; ++stage) {
                    struct d3d_texture_impl *texture = g_textures[stage];
                    struct D3DTexture *header = (struct D3DTexture *)g_texture_sources[stage];
                    fprintf(log, "native_model: t%u header=%08lx format=%u dimensions=%ux%u gl=%u\n", stage,
                        header ? header->Format : 0, texture ? (unsigned int)texture->format : 0,
                        texture ? texture->width : 0, texture ? texture->height : 0, texture ? texture->gl_id : 0);
                }
                fclose(log);
            }
            ++trace_count;
        }
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glUseProgram(program);
    vita_combiner_uniforms(program);
    vita_combiner_matrix(program, 0);
    if (indices) {
        vertex_limit = 0;
        for (first = 0; first < count; ++first)
            if ((unsigned int)indices[first] + 1 > vertex_limit) vertex_limit = indices[first] + 1;
    }
    if (!vita_combiner_vertex_attributes(base, stride, base_vertex, 0, vertex_limit)) { glUseProgram(0); return 1; }
    if (indices) {
        if (primitive_type == D3DPT_QUADLIST) {
            for (first = 0; first + 3 < count; first += 4)
                glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, indices + first);
        } else {
            glDrawElements(d3d_to_gl_prim(primitive_type), (GLsizei)count, GL_UNSIGNED_SHORT, indices);
        }
    } else if (primitive_type == D3DPT_QUADLIST) {
        for (first = 0; first + 3 < count; first += 4)
            glDrawArrays(GL_TRIANGLE_FAN, (GLint)(start_vertex + first), 4);
    } else {
        glDrawArrays(d3d_to_gl_prim(primitive_type), (GLint)start_vertex, (GLsizei)count);
    }
    for (first = 0; first < 16; ++first) glDisableVertexAttribArray(first);
    glUseProgram(0);
    return 1;
}

/* The Linux/Android renderer executes generated NV2A GLSL.  Use the same
 * programmable path for the first fidelity-critical Vita primitive: the
 * dynamically updated A4R4G4B4 character atlas. */
static GLuint vita_text_program_get(void) {
    static const char vertex_source[] =
        "#version 100\n"
        "attribute vec3 a_position;\n"
        "attribute vec4 a_color;\n"
        "attribute vec2 a_texcoord;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "void main(){ gl_Position=vec4(a_position,1.0); v_color=a_color; v_texcoord=a_texcoord; }\n";
    static const char fragment_source[] =
        "#version 100\n"
        "precision mediump float;\n"
        "uniform sampler2D u_texture;\n"
        "varying vec4 v_color;\n"
        "varying vec2 v_texcoord;\n"
        "void main(){ gl_FragColor=texture2D(u_texture,v_texcoord)*v_color; }\n";
    GLuint vertex, fragment, program;
    GLint linked = GL_FALSE;
    char info[1024] = {0};
    FILE *log;
    if (g_text_program_attempted) return g_text_program;
    g_text_program_attempted = 1;
    /* VitaGL's runtime shader compiler aborts on retail hardware for this
     * program: postponed mode dies in glLinkProgram and pair mode dies in
     * glCompileShader.  Keep the known-working fixed-function text path until
     * the NV2A shaders are translated/offline-compiled to a Vita-safe form. */
    vita_shader_log("text shader quarantined; using fixed-function fallback");
    return 0;
#if 0
    vertex = glCreateShader(GL_VERTEX_SHADER);
    fragment = glCreateShader(GL_FRAGMENT_SHADER);
    if (!vertex || !fragment) return 0;
    glShaderSource(vertex, 1, &vertex_source, NULL);
    glShaderSource(fragment, 1, &fragment_source, NULL);
    /* VGL_MODE_SHADER_PAIR requires the two compile calls to be adjacent. */
    vita_shader_log("text shader pair compile begin");
    glCompileShader(vertex);
    glCompileShader(fragment);
    vita_shader_log("text shader pair compile complete");
    if (!vita_shader_status(vertex, "text vertex") ||
        !vita_shader_status(fragment, "text fragment")) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return 0;
    }
    vita_shader_log("text program create begin");
    program = glCreateProgram();
    vita_shader_log("text program attach vertex");
    glAttachShader(program, vertex);
    vita_shader_log("text program attach fragment");
    glAttachShader(program, fragment);
    vita_shader_log("text program bind attributes");
    glBindAttribLocation(program, 0, "a_position");
    glBindAttribLocation(program, 1, "a_color");
    glBindAttribLocation(program, 2, "a_texcoord");
    vita_shader_log("text program link begin");
    glLinkProgram(program);
    vita_shader_log("text program link returned");
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    glGetProgramInfoLog(program, sizeof(info), NULL, info);
    log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "vita_renderer: text program link=%d log=%s\n", (int)linked,
            info[0] ? info : "<empty>");
        fclose(log);
    }
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    if (!linked) {
        glDeleteProgram(program);
        return 0;
    }
    g_text_program = program;
    return program;
#endif
}

static unsigned int vita_texture_mode(unsigned int stage) {
    return (unsigned int)((g_texture_modes >> (stage * 5)) & 31);
}

/* Preserve each Xbox texture stage on its matching VitaGL unit. VitaGL reports
 * two fixed-function units on retail Vita; later Xbox stages stay inactive
 * until a Vita-safe pixel-shader path is available. */
static void vita_prepare_texture(const void *texcoord_pointer, GLsizei texcoord_stride) {
    static unsigned long logged_modes[32];
    static unsigned int logged_mode_count;
    unsigned int stage;
    unsigned int limit = g_ffp_texture_units < 3 ? (unsigned int)g_ffp_texture_units : 3;
    unsigned int upload_limit = g_fragment_texture_units < 4 ? (unsigned int)g_fragment_texture_units : 4;
    unsigned int seen = 0;

    for (stage = 0; stage < upload_limit; ++stage)
        if (g_textures[stage] && !g_textures[stage]->uploaded && g_texture_sources[stage])
            D3DDevice_SetTexture(stage, g_texture_sources[stage]);

    for (stage = 0; stage < logged_mode_count; ++stage)
        if (logged_modes[stage] == g_texture_modes) seen = 1;
    if (!seen && logged_mode_count < 32) {
        FILE *log = fopen("ux0:data/halo/boot.log", "a");
        if (log) {
            fprintf(log, "vita_pass: modes=%08lx ffp=%u", g_texture_modes, limit);
            for (stage = 0; stage < 4; ++stage)
                fprintf(log, " t%u=%u/%u/%d", stage, vita_texture_mode(stage),
                    g_textures[stage] ? (unsigned int)g_textures[stage]->format : 0,
                    g_textures[stage] ? g_textures[stage]->uploaded : 0);
            fprintf(log, "\n");
            fclose(log);
        }
        logged_modes[logged_mode_count++] = g_texture_modes;
    }

    for (stage = 0; stage < 4; ++stage) {
        unsigned int mode = vita_texture_mode(stage);
        int usable = stage < limit && mode != 0 && mode != 4 &&
            g_textures[stage] && g_textures[stage]->uploaded &&
            (!g_render_texture || g_textures[stage]->gl_id != g_render_texture->gl_id);
        if (stage >= limit) continue;
        glActiveTexture(GL_TEXTURE0 + stage);
        glClientActiveTexture(GL_TEXTURE0 + stage);
        if (usable) {
            glBindTexture(GL_TEXTURE_2D, g_textures[stage]->gl_id);
            glEnable(GL_TEXTURE_2D);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                D3D__TextureState[stage][D3DTSS_ADDRESSU] == D3DTADDRESS_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                D3D__TextureState[stage][D3DTSS_ADDRESSV] == D3DTADDRESS_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                D3D__TextureState[stage][D3DTSS_MINFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                D3D__TextureState[stage][D3DTSS_MAGFILTER] == D3DTEXF_POINT ? GL_NEAREST : GL_LINEAR);
            if (texcoord_pointer) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, texcoord_stride, texcoord_pointer);
            }
        } else {
            glDisable(GL_TEXTURE_2D);
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        }
    }
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
}

static void immediate_emit(float x, float y, float z, float w) {
    vita_immediate_vertex *vertex;
    float width;
    float height;
    float *color;
    float *texcoord;
    int component;

    if (!g_immediate_active) return;
    if (g_immediate_count == g_immediate_capacity) {
        unsigned int capacity = g_immediate_capacity ? g_immediate_capacity * 2 : VITA_IMMEDIATE_MAX_VERTICES;
        vita_immediate_vertex *vertices = realloc(g_immediate_vertices, capacity * sizeof(*vertices));
        if (!vertices) {
            /* Drop the entire primitive on allocation failure, not its tail. */
            g_immediate_active = 0; g_immediate_count = 0;
            return;
        }
        g_immediate_vertices = vertices; g_immediate_capacity = capacity;
    }

    vertex = &g_immediate_vertices[g_immediate_count++];
    memcpy(vertex->attributes, g_immediate_attributes, sizeof(vertex->attributes));
    width = 640.0f;
    height = 480.0f;

    /* If screen geometry constants were supplied (SCREEN_GEOMETRY_TRANSFORM_CONSTANT = -68, biased +96 = 28),
     * c28[0] = 2.0f / width, c28[3] = normalized_offset.x - (1.0f / width + 1.0f)
     * c29[1] = -2.0f / height, c29[3] = 1.0f / height + normalized_offset.y + 1.0f */
    if (g_vsh_constants[28][0] > 0.0f && x >= 0.0f && y >= 0.0f) {
        vertex->x = x * g_vsh_constants[28][0] + g_vsh_constants[28][3];
        vertex->y = y * g_vsh_constants[29][1] + g_vsh_constants[29][3];
    } else if (x >= -1.05f && x <= 1.05f && y >= -1.05f && y <= 1.05f) {
        /* Already in NDC coordinates [-1, 1] */
        vertex->x = x;
        vertex->y = y;
    } else {
        /* Screen virtual pixel coordinates in 640x480 */
        vertex->x = (x * 2.0f / width) - 1.0f;
        vertex->y = 1.0f - (y * 2.0f / height);
    }
    vertex->z = (w != 0.0f) ? z / w : z;

    texcoord = g_immediate_attributes[g_immediate_texcoord_register];
    vertex->u = texcoord[0];
    vertex->v = texcoord[1];
    /* Keep raw UVs here. D3DDevice_End sees the complete primitive and can
     * distinguish atlas texels from normalized sprite coordinates without
     * scaling only part of a glyph. */

    color = g_immediate_attributes[g_immediate_color_register];
    for (component = 0; component < 4; component++) {
        float value = color[component];
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        vertex->color[component] = (unsigned char)(value * 255.0f + 0.5f);
    }
}

/* ---------- Lifecycle */

struct Direct3D * WINAPI Direct3DCreate8(unsigned int version) {
    (void)version;
    static int dummy = 1;
    return (struct Direct3D *)&dummy;
}

long WINAPI Direct3D_CreateDevice(
    unsigned int adapter,
    enum _D3DDEVTYPE device_type,
    void *focus_window,
    unsigned long behavior_flags,
    struct _D3DPRESENT_PARAMETERS_ *params,
    struct D3DDevice **returned_device)
{
    (void)adapter; (void)device_type; (void)focus_window; (void)behavior_flags;
    if (!g_vitagl_initialized) {
        vita_shader_log("VitaGL initialization begin; persistent shader cache disabled");
        vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
        vita_shader_log("VitaGL initialization returned");
        g_vitagl_initialized = 1;
        glGetIntegerv(GL_MAX_TEXTURE_UNITS, &g_ffp_texture_units);
        if (g_ffp_texture_units < 1) g_ffp_texture_units = 1;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &g_fragment_texture_units);
        if (g_fragment_texture_units < 1) g_fragment_texture_units = 1;

        {
            FILE *log = fopen("ux0:data/halo/boot.log", "a");
            if (log) {
                fprintf(log, "vita_renderer: revision=81 clamp-argb-color preserve-hs-roots-globals-strings arm-aligned-hs-stack datum-scenario-index-safe-hs-syntax-rebase-exclusion script-thread-dispose-before-node-gc validated-unaligned-string-rebase static-campaign-options null-string-caller-diagnostic direct-map-mounting posix-path-strip 2mb-stack arm-safe-diagnostics double-buffer-audio shader-cache-disabled gl_vendor=%s gl_renderer=%s gl_version=%s glsl=%s\n",
                    (const char *)glGetString(GL_VENDOR), (const char *)glGetString(GL_RENDERER),
                    (const char *)glGetString(GL_VERSION), (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION));
                fprintf(log, "vita_renderer: ffp_texture_units=%d fragment_texture_units=%d xbox_texture_stages=4\n",
                    (int)g_ffp_texture_units, (int)g_fragment_texture_units);
                fclose(log);
            }
        }

        glClearColor(0.025f, 0.03f, 0.04f, 1.0f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CW);
        glViewport(0, 0, 960, 544);
        glDepthRangef(0.0f, 1.0f);

        memset(&g_back_buffer, 0, sizeof(g_back_buffer));
        g_back_buffer.Common = D3DCOMMON_TYPE_SURFACE | 1;
        g_back_buffer.Format = (D3DFMT_LIN_A8R8G8B8 << D3DFORMAT_FORMAT_SHIFT) | (2 << D3DFORMAT_DIMENSION_SHIFT);
        g_back_buffer.Size = ((960 - 1)) | ((544 - 1) << 12);

        memset(&g_depth_buffer, 0, sizeof(g_depth_buffer));
        g_depth_buffer.Common = D3DCOMMON_TYPE_SURFACE | 1;
        g_depth_buffer.Format = (D3DFMT_LIN_D24S8 << D3DFORMAT_FORMAT_SHIFT);
        g_depth_buffer.Size = ((960 - 1)) | ((544 - 1) << 12);

        D3D__RenderState[D3DRS_ZENABLE] = 1;
        D3D__RenderState[D3DRS_ZWRITEENABLE] = 1;
        D3D__RenderState[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
        D3D__RenderState[D3DRS_COLORWRITEENABLE] = 0x01010101;
        D3D__RenderState[D3DRS_CULLMODE] = D3DCULL_CCW;
        D3D__RenderState[D3DRS_FRONTFACE] = D3DFRONT_CW;
        D3D__RenderState[D3DRS_FILLMODE] = D3DFILL_SOLID;
        D3D__RenderState[D3DRS_SRCBLEND] = D3DBLEND_ONE;
        D3D__RenderState[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
        D3D__RenderState[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
        {
            unsigned int reg;
            for (reg = 0; reg < 16; ++reg) g_immediate_attributes[reg][3] = 1.0f;
        }
    }
    if (params) {
        params->BackBufferWidth = 960;
        params->BackBufferHeight = 544;
    }
    if (returned_device) *returned_device = g_d3d_device;
    return S_OK;
}

void WINAPI D3DDevice_Present(const struct tagRECT *src, const struct tagRECT *dst, void *wnd, void *rgn) {
    (void)src; (void)dst; (void)wnd; (void)rgn;
    static unsigned long s_present_num = 0;
    s_present_num++;
    if (s_present_num <= 1) {
        FILE *log = fopen("ux0:data/halo/boot.log", "a");
        if (log) {
            fprintf(log, "d3d8: D3DDevice_Present #%lu enter\n", s_present_num);
            fclose(log);
        }
    }
    vglSwapBuffers(GL_FALSE);
    vita_flip_count++;
    if (s_present_num <= 1) {
        FILE *log = fopen("ux0:data/halo/boot.log", "a");
        if (log) {
            fprintf(log, "d3d8: D3DDevice_Present #%lu vglSwapBuffers complete\n", s_present_num);
            fclose(log);
        }
    }
    if (g_vblank_callback) {
        g_vblank_callback(0);
    }
}

void WINAPI D3DDevice_Clear(unsigned long count, const struct _D3DRECT *rects, unsigned long flags, unsigned long color, float z, unsigned long stencil) {
    (void)count; (void)rects;
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        float a = ((color >> 24) & 0xff) * (1.0f / 255.0f);
        float r = ((color >> 16) & 0xff) * (1.0f / 255.0f);
        float g = ((color >> 8) & 0xff) * (1.0f / 255.0f);
        float b = (color & 0xff) * (1.0f / 255.0f);
        glColorMask((flags & D3DCLEAR_TARGET_R) != 0,
            (flags & D3DCLEAR_TARGET_G) != 0,
            (flags & D3DCLEAR_TARGET_B) != 0,
            (flags & D3DCLEAR_TARGET_A) != 0);
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        glDepthMask(GL_TRUE);
        glClearDepthf(z);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_STENCIL) {
        glClearStencil((GLint)stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask) glClear(mask);
    vita_apply_render_states();
}

void WINAPI D3DDevice_Begin(enum _D3DPRIMITIVETYPE type) {
    g_immediate_type = type;
    g_immediate_count = 0;
    g_immediate_active = 1;
    g_immediate_color_register = D3DVSDE_DIFFUSE;
    g_immediate_texcoord_register = D3DVSDE_TEXCOORD0;
    g_immediate_attributes[D3DVSDE_DIFFUSE][0] = 1.0f;
    g_immediate_attributes[D3DVSDE_DIFFUSE][1] = 1.0f;
    g_immediate_attributes[D3DVSDE_DIFFUSE][2] = 1.0f;
    g_immediate_attributes[D3DVSDE_DIFFUSE][3] = 1.0f;
}
void WINAPI D3DDevice_End(void) {
    int is_text_atlas;
    unsigned int vertex_index;
    float maximum_u = 0.0f, maximum_v = 0.0f;
    GLuint text_program = 0;
    GLuint pixel_program = 0;
    if (!g_immediate_active) return;
    g_immediate_active = 0;
    if (!g_immediate_count) return;

    static unsigned long s_draw_num = 0;
    s_draw_num++;
    if (s_draw_num <= 1) {
        FILE *log = fopen("ux0:data/halo/boot.log", "a");
        if (log) {
            fprintf(log, "immediate_draw #%lu: type=%d count=%u\n", s_draw_num, (int)g_immediate_type, g_immediate_count);
            fclose(log);
        }
    }

    if (g_immediate_type == D3DPT_QUADLIST && g_immediate_count < 4) return;
    if (g_immediate_type == D3DPT_TRIANGLEFAN && g_immediate_count < 3) return;
    if (g_immediate_type == D3DPT_TRIANGLELIST && g_immediate_count < 3) return;
    if (g_immediate_type == D3DPT_TRIANGLESTRIP && g_immediate_count < 3) return;

    vita_apply_render_states();

    /* 2D Immediate mode uses normalized device coordinates [-1, 1] */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    /* Disable backface culling for 2D UI quads to ensure both windings display */
    glDisable(GL_CULL_FACE);

    /* If depth testing is disabled for UI, ensure depth mask is also disabled */
    if (!D3D__RenderState[D3DRS_ZENABLE]) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    /* Set texture stage 0 */
    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    if (g_textures[0] && g_textures[0]->gl_id) {
        glBindTexture(GL_TEXTURE_2D, g_textures[0]->gl_id);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    /* Text uses a D3D-created, dynamically updated A4 atlas; map sprites use
     * cache-backed bitmap headers. Register numbers and UV range alone cannot
     * reliably distinguish the two paths. */
    is_text_atlas = g_immediate_texcoord_register == 4 && g_immediate_color_register == 9 &&
        g_texture_sources[0] && (g_texture_sources[0]->Common & D3DCOMMON_D3DCREATED) &&
        g_textures[0] && (g_textures[0]->format == D3DFMT_A4R4G4B4 ||
            g_textures[0]->format == D3DFMT_LIN_A4R4G4B4);
    if (g_immediate_texcoord_register == 4) {
        for (vertex_index = 0; vertex_index < g_immediate_count; ++vertex_index) {
            float u = g_immediate_vertices[vertex_index].u;
            float v = g_immediate_vertices[vertex_index].v;
            float au = u < 0.0f ? -u : u;
            float av = v < 0.0f ? -v : v;
            if (au > maximum_u) maximum_u = au;
            if (av > maximum_v) maximum_v = av;
        }
    }
    if (!is_text_atlas && vita_needs_combiner()) {
        struct nv2a_pixel_shader_key key;
        vita_pixel_key(&key);
        pixel_program = vita_combiner_program_get(&key);
        if (pixel_program) {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glUseProgram(pixel_program);
            vita_combiner_uniforms(pixel_program);
            vita_combiner_matrix(pixel_program, 1);
            vita_combiner_vertex_attributes(NULL, 0, 0, 1, g_immediate_count);
        }
    }
    if (is_text_atlas) {
        /* DrawString submits atlas texel coordinates. Normalize every vertex
         * together using constants set by the Xbox text renderer. Sprites,
         * including A4 map bitmaps, keep their normalized 0..1 coordinates. */
        if (g_vsh_constants[32][0] > 0.0f && g_vsh_constants[32][1] > 0.0f) {
            for (vertex_index = 0; vertex_index < g_immediate_count; ++vertex_index) {
                g_immediate_vertices[vertex_index].u *= g_vsh_constants[32][0];
                g_immediate_vertices[vertex_index].v *= g_vsh_constants[32][1];
            }
        }
    }

    {
        static unsigned int immediate_state_log_count;
        if (immediate_state_log_count < 48 && g_textures[0]) {
            FILE *log = fopen("ux0:data/halo/boot.log", "a");
            if (log) {
                fprintf(log, "immediate_state: draw=%u primitive=%d count=%u modes=%08lx regs=%d/%d "
                    "source_created=%d format=%u size=%ux%u uv_max=%g,%g text=%d\n",
                    immediate_state_log_count, (int)g_immediate_type, g_immediate_count, g_texture_modes,
                    g_immediate_texcoord_register, g_immediate_color_register,
                    g_texture_sources[0] ? !!(g_texture_sources[0]->Common & D3DCOMMON_D3DCREATED) : 0,
                    (unsigned int)g_textures[0]->format, g_textures[0]->width, g_textures[0]->height,
                    maximum_u, maximum_v, is_text_atlas);
                fclose(log);
            }
            ++immediate_state_log_count;
        }
    }

    if (!pixel_program) {
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(vita_immediate_vertex), &g_immediate_vertices[0].x);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vita_immediate_vertex), g_immediate_vertices[0].color);
        glTexCoordPointer(2, GL_FLOAT, sizeof(vita_immediate_vertex), &g_immediate_vertices[0].u);
        vita_prepare_texture(&g_immediate_vertices[0].u, sizeof(vita_immediate_vertex));
    }
    {
        static int text_state_logged;
        if (is_text_atlas) {
            /* Xbox text relies on the A4 component of this atlas.  Some of
             * its tiny combiner programs do not leave a dependable render-
             * state shadow, so enforce the equivalent fixed-function blend. */
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        if (!text_state_logged && is_text_atlas) {
            struct d3d_texture_impl *texture = g_textures[0];
            FILE *log = fopen("ux0:data/halo/boot.log", "a");
            if (log) {
                unsigned int opposite = g_immediate_count > 2 ? 2 : 0;
                unsigned int zero_alpha = 0, partial_alpha = 0, full_alpha = 0, texel;
                const unsigned char *pixels = (const unsigned char *)texture->cpu_data;
                for (texel = 0; pixels && texel + 1 < texture->data_size; texel += 2) {
                    unsigned int alpha = pixels[texel + 1] >> 4;
                    if (!alpha) ++zero_alpha;
                    else if (alpha == 15) ++full_alpha;
                    else ++partial_alpha;
                }
                fprintf(log, "text_draw: modes=0x%08lx texture=%p uploaded=%d size=%ux%u format=%u "
                    "scale=%g,%g uv0=%g,%g uv2=%g,%g alpha=%u/%u/%u\n", g_texture_modes, (void *)texture,
                    texture ? texture->uploaded : 0, texture ? texture->width : 0,
                    texture ? texture->height : 0, texture ? (unsigned)texture->format : 0,
                    g_vsh_constants[32][0], g_vsh_constants[32][1],
                    g_immediate_vertices[0].u, g_immediate_vertices[0].v,
                    g_immediate_vertices[opposite].u, g_immediate_vertices[opposite].v,
                    zero_alpha, partial_alpha, full_alpha);
                fclose(log);
            }
            text_state_logged = 1;
        }
    }
    if (is_text_atlas) {
        text_program = vita_text_program_get();
        if (text_program) {
            glDisableClientState(GL_TEXTURE_COORD_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glDisableClientState(GL_VERTEX_ARRAY);
            glUseProgram(text_program);
            glUniform1i(glGetUniformLocation(text_program, "u_texture"), 0);
            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vita_immediate_vertex), &g_immediate_vertices[0].x);
            glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(vita_immediate_vertex), g_immediate_vertices[0].color);
            glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(vita_immediate_vertex), &g_immediate_vertices[0].u);
        }
    }
    if (g_immediate_type == D3DPT_QUADLIST) {
        unsigned int first;
        for (first = 0; first + 3 < g_immediate_count; first += 4)
            glDrawArrays(GL_TRIANGLE_FAN, (GLint)first, 4);
    } else {
        glDrawArrays(d3d_to_gl_prim(g_immediate_type), 0, (GLsizei)g_immediate_count);
    }
    if (pixel_program) {
        unsigned int reg;
        for (reg = 0; reg < 16; ++reg) glDisableVertexAttribArray(reg);
        glUseProgram(0);
    }
    if (text_program) {
        glDisableVertexAttribArray(2);
        glDisableVertexAttribArray(1);
        glDisableVertexAttribArray(0);
        glUseProgram(0);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}
unsigned long WINAPI D3DDevice_Release(void) { return 0; }
long WINAPI D3DDevice_PersistDisplay(void) { return S_OK; }
void WINAPI Direct3D_SetPushBufferSize(unsigned long psize, unsigned long usize) { (void)psize; (void)usize; }

/* ---------- Viewport & Targets */

void WINAPI D3DDevice_SetViewport(const struct _D3DVIEWPORT8 *viewport) {
    if (!viewport) return;
    g_viewport = *viewport;
    vita_update_viewport_constants();
    if (g_render_texture) {
        glViewport(viewport->X, (int)g_render_texture->height - (int)(viewport->Y + viewport->Height), viewport->Width, viewport->Height);
    } else if (viewport->Width == 640 && viewport->Height == 480 && viewport->X == 0 && viewport->Y == 0) {
        glViewport(0, 0, 960, 544);
    } else {
        int x = (int)((float)viewport->X * (960.0f / 640.0f) + 0.5f);
        int w = (int)((float)viewport->Width * (960.0f / 640.0f) + 0.5f);
        int y = (int)((float)viewport->Y * (544.0f / 480.0f) + 0.5f);
        int h = (int)((float)viewport->Height * (544.0f / 480.0f) + 0.5f);
        glViewport(x, 544 - (y + h), w, h);
    }
    glDepthRangef(viewport->MinZ, viewport->MaxZ);
}

void WINAPI D3DDevice_SetTransform(enum _D3DTRANSFORMSTATETYPE state, const struct _D3DMATRIX *matrix) {
    (void)state; (void)matrix;
}

void WINAPI D3DDevice_GetTransform(enum _D3DTRANSFORMSTATETYPE state, struct _D3DMATRIX *matrix) {
    (void)state;
    if (matrix) {
        memset(matrix, 0, sizeof(*matrix));
        matrix->_11 = matrix->_22 = matrix->_33 = matrix->_44 = 1.0f;
    }
}

void WINAPI D3DDevice_GetBackBuffer(int back_buffer, unsigned long type, struct D3DSurface **back_buffer_surface) {
    (void)back_buffer; (void)type;
    if (back_buffer_surface) *back_buffer_surface = &g_back_buffer;
}

long WINAPI D3DDevice_GetDepthStencilSurface(struct D3DSurface **z_stencil_surface) {
    if (z_stencil_surface) *z_stencil_surface = &g_depth_buffer;
    return S_OK;
}

void WINAPI D3DDevice_SetRenderTarget(struct D3DSurface *render_target, struct D3DSurface *new_z_stencil) {
    if (g_render_texture && g_render_texture->surface_owner && g_render_texture->surface_level) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_render_texture->surface_owner->gl_id);
        glCopyTexSubImage2D(GL_TEXTURE_2D, g_render_texture->surface_level, 0, 0, 0, 0,
            g_render_texture->width, g_render_texture->height);
    }
    if (!render_target || render_target == &g_back_buffer) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        g_render_texture = NULL;
    } else {
        struct d3d_texture_impl *impl = vita_texture_binding_get((struct D3DBaseTexture *)render_target);
        if (!impl) return;
        if (!impl->framebuffer) {
            struct d3d_texture_impl *owner = impl->surface_owner ? impl->surface_owner : impl;
            if (!owner->gl_id) glGenTextures(1, &owner->gl_id);
            impl->gl_id = owner->gl_id;
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, impl->gl_id);
            glTexImage2D(GL_TEXTURE_2D, impl->surface_level, GL_RGBA, impl->width, impl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            /* VitaGL's framebuffer attachment ignores its level argument.
             * Render nonzero levels into a level-zero scratch texture and
             * copy into the owning mip when leaving this render target. */
            if (impl->surface_level) {
                glGenTextures(1, &impl->gl_id);
                glBindTexture(GL_TEXTURE_2D, impl->gl_id);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, impl->width, impl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            }
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glGenFramebuffers(1, &impl->framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, impl->framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, impl->gl_id, 0);
            owner->uploaded = 1;
            impl->uploaded = 1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, impl->framebuffer);
        if (new_z_stencil && !impl->depthbuffer) {
            glGenRenderbuffers(1, &impl->depthbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, impl->depthbuffer);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, impl->width, impl->height);
        }
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
            new_z_stencil ? impl->depthbuffer : 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_render_texture ? g_render_texture->framebuffer : 0);
            return;
        }
        g_render_texture = impl;
    }
    D3DDevice_SetViewport(&g_viewport);
}

void WINAPI D3DSurface_GetDesc(struct D3DSurface *surface, struct _D3DSURFACE_DESC *desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->Width = 960;
    desc->Height = 544;
    desc->Format = D3DFMT_LIN_A8R8G8B8;
    desc->Type = D3DRTYPE_SURFACE;
    if (surface && surface != &g_back_buffer && surface != &g_depth_buffer) {
        struct d3d_texture_impl *impl = vita_texture_binding_get((struct D3DBaseTexture *)surface);
        if (impl) { desc->Width = impl->width; desc->Height = impl->height; desc->Format = impl->format; }
    }
}

void WINAPI D3DSurface_LockRect(struct D3DSurface *surface, struct _D3DLOCKED_RECT *locked_rect, const struct tagRECT *rect, unsigned long flags) {
    (void)surface; (void)rect; (void)flags;
    if (locked_rect) {
        locked_rect->Pitch = 960 * 4;
        locked_rect->pBits = (void *)0;
    }
}

/* ---------- Buffers & Streams */

struct d3d_vertex_buffer_impl {
    struct D3DVertexBuffer base;
    unsigned int size;
    unsigned long usage;
    unsigned long fvf;
    void *cpu_data;
};

struct d3d_index_buffer_impl {
    struct D3DIndexBuffer base;
    unsigned int size;
    unsigned long usage;
    enum _D3DFORMAT format;
    void *cpu_data;
};

static struct d3d_index_buffer_impl *g_current_ib = NULL;
static unsigned int g_base_vertex;

/* Use declared offsets and streams for the fixed-function fallback. Packed
 * normals and Xbox microcode still require the programmable shader path. */
static void vita_apply_vertex_declaration(unsigned int base_vertex) {
    unsigned int reg;
    if (!g_vertex_declaration) return;
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glColor4f(1, 1, 1, 1);
    for (reg = 0; reg < 16; ++reg) {
        struct vita_vertex_element *element = &g_vertex_declaration->elements[reg];
        const unsigned char *pointer;
        unsigned int count = element->type >> 4;
        if (!element->present || !g_streams[element->stream] || !g_streams[element->stream]->Data) continue;
        pointer = (const unsigned char *)g_streams[element->stream]->Data + element->offset +
            base_vertex * g_stream_strides[element->stream];
        if (count < 1 || count > 4) continue;
        if (reg == 0 && (element->type & 15) == 2 && count >= 2) {
            glVertexPointer(count, GL_FLOAT, g_stream_strides[element->stream], pointer);
        } else if (reg == 3 && element->type == D3DVSDT_D3DCOLOR) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(4, GL_UNSIGNED_BYTE, g_stream_strides[element->stream], pointer);
        } else if (reg == 3 && (element->type & 15) == 2 && count >= 3) {
            glEnableClientState(GL_COLOR_ARRAY);
            glColorPointer(count, GL_FLOAT, g_stream_strides[element->stream], pointer);
        } else if (reg == 9 && (element->type & 15) == 2 && count >= 2) {
            unsigned int stage;
            unsigned int limit = g_ffp_texture_units < 3 ? (unsigned int)g_ffp_texture_units : 3;
            for (stage = 0; stage < limit; ++stage) {
                if (!vita_texture_mode(stage)) continue;
                glClientActiveTexture(GL_TEXTURE0 + stage);
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(count, GL_FLOAT, g_stream_strides[element->stream], pointer);
            }
            glClientActiveTexture(GL_TEXTURE0);
        }
    }
}

long WINAPI D3DDevice_CreateVertexBuffer(unsigned int length, unsigned long usage, unsigned long fvf, unsigned long pool, struct D3DVertexBuffer **ppVertexBuffer) {
    (void)pool;
    struct d3d_vertex_buffer_impl *vb = (struct d3d_vertex_buffer_impl *)calloc(1, sizeof(*vb));
    if (!vb) return E_OUTOFMEMORY;
    vb->base.Common = D3DCOMMON_TYPE_VERTEXBUFFER | D3DCOMMON_D3DCREATED | 1;
    vb->size = length;
    vb->usage = usage;
    vb->fvf = fvf;
    vb->cpu_data = halo_vita_alloc(length, 64, UINT32_MAX, PAGE_READWRITE);
    if (!vb->cpu_data) vb->cpu_data = malloc(length);
    vb->base.Data = (DWORD)vb->cpu_data;
    if (ppVertexBuffer) *ppVertexBuffer = (struct D3DVertexBuffer *)vb;
    return S_OK;
}

void WINAPI D3DVertexBuffer_Lock(struct D3DVertexBuffer *buffer, unsigned int offset, unsigned int size, unsigned char **data, unsigned long flags) {
    (void)flags; (void)size;
    if (data && buffer) {
        *data = (unsigned char *)buffer->Data + offset;
    }
}

long WINAPI D3DDevice_CreateIndexBuffer(unsigned int length, unsigned long usage, enum _D3DFORMAT format, unsigned long pool, struct D3DIndexBuffer **ppIndexBuffer) {
    (void)pool;
    struct d3d_index_buffer_impl *ib = (struct d3d_index_buffer_impl *)calloc(1, sizeof(*ib));
    if (!ib) return E_OUTOFMEMORY;
    ib->base.Common = D3DCOMMON_TYPE_INDEXBUFFER | D3DCOMMON_D3DCREATED | 1;
    ib->size = length;
    ib->usage = usage;
    ib->format = format;
    ib->cpu_data = halo_vita_alloc(length, 64, UINT32_MAX, PAGE_READWRITE);
    if (!ib->cpu_data) ib->cpu_data = malloc(length);
    ib->base.Data = (DWORD)ib->cpu_data;
    if (ppIndexBuffer) *ppIndexBuffer = (struct D3DIndexBuffer *)ib;
    return S_OK;
}

void WINAPI D3DDevice_SetStreamSource(unsigned int stream_number, struct D3DVertexBuffer *stream_data, unsigned int stride) {
    if (stream_number < 16) {
        g_streams[stream_number] = stream_data;
        g_stream_strides[stream_number] = stride;
    }
}

void WINAPI D3DDevice_SetIndices(struct D3DIndexBuffer *index_data, unsigned int base_vertex_index) {
    g_base_vertex = base_vertex_index;
    g_current_ib = (struct d3d_index_buffer_impl *)index_data;
    D3D__IndexData = index_data ? (WORD *)index_data->Data : NULL;
}

/* Map textures are only Xbox headers. Never append host state to them. */
struct vita_texture_binding {
    struct D3DBaseTexture *header;
    DWORD data, format, size;
    struct d3d_texture_impl host;
};
static struct vita_texture_binding texture_bindings[256];
static unsigned int texture_binding_next;

/* ---------- Xbox Texture Decoding & Unswizzling */

struct vita_swizzle_masks {
    unsigned long x, y, z;
};

static struct vita_swizzle_masks vita_swizzle_masks(unsigned long width, unsigned long height, unsigned long depth) {
    struct vita_swizzle_masks masks = { 0, 0, 0 };
    unsigned long bit = 1, mask_bit = 1;
    int done;
    do {
        done = 1;
        if (bit < width) {
            masks.x |= mask_bit;
            mask_bit <<= 1;
            done = 0;
        }
        if (bit < height) {
            masks.y |= mask_bit;
            mask_bit <<= 1;
            done = 0;
        }
        if (bit < depth) {
            masks.z |= mask_bit;
            mask_bit <<= 1;
            done = 0;
        }
        bit <<= 1;
    } while (!done);
    return masks;
}

static unsigned long vita_spread(unsigned long mask, unsigned long value) {
    unsigned long result = 0, bit = 1;
    while (value && bit) {
        if (mask & bit) {
            if (value & 1)
                result |= bit;
            value >>= 1;
        }
        bit <<= 1;
    }
    return result;
}

static inline unsigned long vita_expand5(unsigned long v) { return (v << 3) | (v >> 2); }
static inline unsigned long vita_expand6(unsigned long v) { return (v << 2) | (v >> 4); }
static inline unsigned long vita_expand4(unsigned long v) { return v * 0x11; }

static inline unsigned long vita_make_rgba(unsigned long r, unsigned long g, unsigned long b, unsigned long a) {
    return (a << 24) | (b << 16) | (g << 8) | r;
}

/* ---------- DXT Software Decoding (Vita SGX543 has NO S3TC hardware support) */

static unsigned long vita_color565_to_rgba(unsigned long v) {
    return vita_make_rgba(vita_expand5(v >> 11), vita_expand6((v >> 5) & 0x3f), vita_expand5(v & 0x1f), 255);
}

static unsigned long vita_color_mix(unsigned long a, unsigned long b, unsigned long wa, unsigned long wb, unsigned long div) {
    unsigned long result = 0;
    int shift;
    for (shift = 0; shift < 32; shift += 8) {
        unsigned long ch = (((a >> shift) & 0xff) * wa + ((b >> shift) & 0xff) * wb) / div;
        result |= (ch & 0xff) << shift;
    }
    return result;
}

static void vita_dxt_color_block(const unsigned char *block, int dxt1, unsigned long colors[16]) {
    unsigned long c0 = block[0] | ((unsigned long)block[1] << 8);
    unsigned long c1 = block[2] | ((unsigned long)block[3] << 8);
    unsigned long palette[4];
    unsigned long bits = block[4] | ((unsigned long)block[5] << 8) |
                        ((unsigned long)block[6] << 16) | ((unsigned long)block[7] << 24);
    int i;

    palette[0] = vita_color565_to_rgba(c0);
    palette[1] = vita_color565_to_rgba(c1);
    if (c0 > c1 || !dxt1) {
        palette[2] = vita_color_mix(palette[0], palette[1], 2, 1, 3);
        palette[3] = vita_color_mix(palette[0], palette[1], 1, 2, 3);
    } else {
        palette[2] = vita_color_mix(palette[0], palette[1], 1, 1, 2);
        palette[3] = 0; /* transparent black */
    }
    for (i = 0; i < 16; i++)
        colors[i] = palette[(bits >> (i * 2)) & 3];
}

static void vita_dxt_decode(enum _D3DFORMAT format, const unsigned char *src,
                            unsigned long width, unsigned long height, unsigned long *dst) {
    unsigned long blocks_x = (width + 3) / 4;
    unsigned long blocks_y = (height + 3) / 4;
    unsigned long block_bytes = (format == D3DFMT_DXT1) ? 8 : 16;
    unsigned long bx, by, x, y;
    int i;

    for (by = 0; by < blocks_y; by++) {
        for (bx = 0; bx < blocks_x; bx++) {
            const unsigned char *block = src + (by * blocks_x + bx) * block_bytes;
            unsigned long colors[16];
            unsigned long alpha[16];

            if (format == D3DFMT_DXT1) {
                vita_dxt_color_block(block, 1, colors);
                for (i = 0; i < 16; i++)
                    alpha[i] = (colors[i] >> 24) & 0xff;
            } else {
                /* DXT3/DXT5: color block is at offset +8 */
                vita_dxt_color_block(block + 8, 0, colors);

                if (format == D3DFMT_DXT2 || format == D3DFMT_DXT3) {
                    /* DXT3: explicit 4-bit alpha per texel */
                    for (i = 0; i < 16; i++)
                        alpha[i] = vita_expand4((block[i / 2] >> ((i & 1) * 4)) & 0xf);
                } else {
                    /* DXT5: interpolated alpha */
                    unsigned long a0 = block[0], a1 = block[1];
                    unsigned long values[8];
                    unsigned long long bits = 0;
                    int bit;

                    for (bit = 0; bit < 6; bit++)
                        bits |= (unsigned long long)block[2 + bit] << (bit * 8);

                    values[0] = a0;
                    values[1] = a1;
                    if (a0 > a1) {
                        for (i = 2; i < 8; i++)
                            values[i] = ((8 - i) * a0 + (i - 1) * a1) / 7;
                    } else {
                        for (i = 2; i < 6; i++)
                            values[i] = ((6 - i) * a0 + (i - 1) * a1) / 5;
                        values[6] = 0;
                        values[7] = 255;
                    }
                    for (i = 0; i < 16; i++)
                        alpha[i] = values[(bits >> (i * 3)) & 7];
                }
            }

            /* Write the 4x4 block into the destination */
            for (y = 0; y < 4; y++) {
                for (x = 0; x < 4; x++) {
                    unsigned long px = bx * 4 + x, py = by * 4 + y;
                    if (px < width && py < height) {
                        dst[py * width + px] =
                            (colors[y * 4 + x] & 0x00ffffffUL) | (alpha[y * 4 + x] << 24);
                    }
                }
            }
        }
    }
}

/* ---------- Texel Conversion */

static unsigned long vita_convert_texel(enum _D3DFORMAT format, const unsigned char *src,
                                        const unsigned long *palette) {
    unsigned long v16 = 0;
    if (format == D3DFMT_R5G6B5 || format == D3DFMT_LIN_R5G6B5 ||
        format == D3DFMT_A1R5G5B5 || format == D3DFMT_LIN_A1R5G5B5 ||
        format == D3DFMT_A4R4G4B4 || format == D3DFMT_LIN_A4R4G4B4)
        v16 = src[0] | ((unsigned long)src[1] << 8);

    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_LIN_A8R8G8B8:
        return vita_make_rgba(src[2], src[1], src[0], src[3]);

    case D3DFMT_X8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
        return vita_make_rgba(src[2], src[1], src[0], 255);

    case D3DFMT_R5G6B5:
    case D3DFMT_LIN_R5G6B5:
        return vita_make_rgba(vita_expand5(v16 >> 11), vita_expand6((v16 >> 5) & 0x3f), vita_expand5(v16 & 0x1f), 255);

    case D3DFMT_A1R5G5B5:
    case D3DFMT_LIN_A1R5G5B5:
        return vita_make_rgba(vita_expand5((v16 >> 10) & 0x1f), vita_expand5((v16 >> 5) & 0x1f), vita_expand5(v16 & 0x1f), (v16 & 0x8000) ? 255 : 0);

    case D3DFMT_A4R4G4B4:
    case D3DFMT_LIN_A4R4G4B4:
        return vita_make_rgba(vita_expand4((v16 >> 8) & 0xf), vita_expand4((v16 >> 4) & 0xf), vita_expand4(v16 & 0xf), vita_expand4(v16 >> 12));

    case D3DFMT_A8:
    case D3DFMT_LIN_A8:
        return vita_make_rgba(255, 255, 255, src[0]);

    case D3DFMT_L8:
    case D3DFMT_LIN_L8:
        return vita_make_rgba(src[0], src[0], src[0], 255);

    case D3DFMT_AL8:
    case D3DFMT_LIN_AL8:
        return vita_make_rgba(src[0], src[0], src[0], src[0]);

    case D3DFMT_A8L8:
    case D3DFMT_LIN_A8L8:
        return vita_make_rgba(src[0], src[0], src[0], src[1]);

    case D3DFMT_P8:
        if (palette) {
            unsigned long color = palette[src[0]];
            return vita_make_rgba((color >> 16) & 0xff, (color >> 8) & 0xff,
                                  color & 0xff, color >> 24);
        }
        return vita_make_rgba(src[0], src[0], src[0], 255);

    default:
        return vita_make_rgba(src[0], src[1], src[2], src[3]);
    }
}

static unsigned int vita_format_bytes_per_pixel(enum _D3DFORMAT format) {
    switch (format) {
    case D3DFMT_A8:
    case D3DFMT_LIN_A8:
    case D3DFMT_L8:
    case D3DFMT_LIN_L8:
    case D3DFMT_AL8:
    case D3DFMT_LIN_AL8:
    case D3DFMT_P8:
        return 1;
    case D3DFMT_R5G6B5:
    case D3DFMT_LIN_R5G6B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_LIN_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_LIN_A4R4G4B4:
    case D3DFMT_A8L8:
    case D3DFMT_LIN_A8L8:
        return 2;
    default:
        return 4;
    }
}

static int vita_is_format_linear(enum _D3DFORMAT format, unsigned long size) {
    /* Created textures carry a synthetic Size word, but Halo swizzles their
     * pixels explicitly in rasterizer_xbox_hardware_bitmaps.c before unlock.
     * Their raw format still determines layout. */
    (void)size;
    switch (format) {
    case D3DFMT_LIN_A8R8G8B8:
    case D3DFMT_LIN_X8R8G8B8:
    case D3DFMT_LIN_R5G6B5:
    case D3DFMT_LIN_A1R5G5B5:
    case D3DFMT_LIN_A4R4G4B4:
    case D3DFMT_LIN_A8:
    case D3DFMT_LIN_L8:
    case D3DFMT_LIN_AL8:
    case D3DFMT_LIN_A8L8:
        return 1;
    default:
        return 0;
    }
}

static void vita_decode_uncompressed(struct d3d_texture_impl *impl, unsigned long size,
                                     unsigned long *dst_rgba, const unsigned long *palette) {
    unsigned int width = impl->width;
    unsigned int height = impl->height;
    unsigned int bpp = vita_format_bytes_per_pixel(impl->format);
    const unsigned char *src = (const unsigned char *)impl->cpu_data;
    unsigned int x, y;

    if (vita_is_format_linear(impl->format, size)) {
        unsigned int pitch = impl->pitch ? impl->pitch : (width * bpp);
        for (y = 0; y < height; y++) {
            const unsigned char *row = src + y * pitch;
            for (x = 0; x < width; x++) {
                dst_rgba[y * width + x] = vita_convert_texel(impl->format, row + x * bpp, palette);
            }
        }
    } else {
        struct vita_swizzle_masks masks = vita_swizzle_masks(width, height, 1);
        unsigned long *x_offsets = (unsigned long *)malloc(width * sizeof(unsigned long));
        if (!x_offsets) return;
        for (x = 0; x < width; x++)
            x_offsets[x] = vita_spread(masks.x, x);
        for (y = 0; y < height; y++) {
            unsigned long y_offset = vita_spread(masks.y, y);
            for (x = 0; x < width; x++) {
                const unsigned char *texel = src + (x_offsets[x] | y_offset) * bpp;
                dst_rgba[y * width + x] = vita_convert_texel(impl->format, texel, palette);
            }
        }
        free(x_offsets);
    }
}

static int vita_is_dxt_format(enum _D3DFORMAT format) {
    return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 ||
           format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

static struct d3d_texture_impl *vita_texture_binding_get(struct D3DBaseTexture *texture) {
    if (!texture) return NULL;
    if (texture->Common & D3DCOMMON_D3DCREATED) {
        struct D3DTexture *header = (struct D3DTexture *)texture;
        if (((header->Format >> D3DFORMAT_DIMENSION_SHIFT) & 15) != 2)
            return NULL; /* Volume/cube allocations have a different layout. */
        return (struct d3d_texture_impl *)texture;
    }
    struct D3DTexture *header = (struct D3DTexture *)texture;
    struct vita_texture_binding *binding = NULL;
    unsigned int i;
    for (i = 0; i < 256; ++i) {
        if (texture_bindings[i].header == texture) { binding = &texture_bindings[i]; break; }
    }
    if (!binding) {
        unsigned int attempt, stage;
        for (attempt = 0; attempt < 256; ++attempt) {
            struct vita_texture_binding *candidate = &texture_bindings[texture_binding_next++ % 256];
            int pinned = g_render_texture == &candidate->host;
            for (stage = 0; stage < D3DTSS_MAXSTAGES; ++stage)
                if (g_textures[stage] == &candidate->host) pinned = 1;
            if (!pinned) { binding = candidate; break; }
        }
        if (!binding) return NULL;
    }
    if (binding->header != texture || binding->data != header->Data ||
        binding->format != header->Format || binding->size != header->Size) {
        if (binding->host.framebuffer) glDeleteFramebuffers(1, &binding->host.framebuffer);
        if (binding->host.depthbuffer) glDeleteRenderbuffers(1, &binding->host.depthbuffer);
        if (binding->host.gl_id) glDeleteTextures(1, &binding->host.gl_id);
        memset(binding, 0, sizeof(*binding));
        binding->header = texture;
        binding->data = header->Data;
        binding->format = header->Format;
        binding->size = header->Size;
        binding->host.format = (header->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT;
        binding->host.width = header->Size ? (header->Size & D3DSIZE_WIDTH_MASK) + 1 :
            1u << ((header->Format & D3DFORMAT_USIZE_MASK) >> D3DFORMAT_USIZE_SHIFT);
        binding->host.height = header->Size ? ((header->Size & D3DSIZE_HEIGHT_MASK) >> D3DSIZE_HEIGHT_SHIFT) + 1 :
            1u << ((header->Format & D3DFORMAT_VSIZE_MASK) >> D3DFORMAT_VSIZE_SHIFT);
        binding->host.pitch = header->Size ? ((((header->Size & D3DSIZE_PITCH_MASK) >> D3DSIZE_PITCH_SHIFT) + 1) * 64) : 0;
        binding->host.cpu_data = (void *)header->Data;
        if (binding->host.format == D3DFMT_DXT1) {
            binding->host.data_size = ((binding->host.width + 3) / 4) * ((binding->host.height + 3) / 4) * 8;
        } else if (binding->host.format == D3DFMT_DXT2 || binding->host.format == D3DFMT_DXT3 ||
                   binding->host.format == D3DFMT_DXT4 || binding->host.format == D3DFMT_DXT5) {
            binding->host.data_size = ((binding->host.width + 3) / 4) * ((binding->host.height + 3) / 4) * 16;
        } else {
            binding->host.data_size = binding->host.width * binding->host.height *
                vita_format_bytes_per_pixel(binding->host.format);
        }
    }
    return &binding->host;
}

long WINAPI D3DDevice_CreateTexture(unsigned int width, unsigned int height, unsigned int levels, unsigned long usage, enum _D3DFORMAT format, unsigned long pool, struct D3DTexture **ppTexture) {
    (void)usage; (void)pool;
    struct d3d_texture_impl *tex = (struct d3d_texture_impl *)calloc(1, sizeof(*tex));
    if (!tex) return E_OUTOFMEMORY;
    tex->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    tex->width = width;
    tex->height = height;
    if (!ppTexture || !width || !height || width > 4096 || height > 4096) {
        free(tex);
        return E_INVALIDARG;
    }
    unsigned int max_levels = 1, extent = width > height ? width : height;
    while (extent > 1) { extent >>= 1; ++max_levels; }
    tex->levels = levels ? levels : max_levels;
    if (tex->levels > max_levels) { free(tex); return E_INVALIDARG; }
    tex->format = format;

    unsigned int bpp = vita_format_bytes_per_pixel(format);
    unsigned int level, w = width, h = height;
    for (level = 0; level < tex->levels; ++level) {
        unsigned int rows = h;
        tex->level_offsets[level] = tex->data_size;
        tex->level_pitches[level] = w * bpp;
        if (vita_is_dxt_format(format)) {
            tex->level_pitches[level] = ((w + 3) / 4) * (format == D3DFMT_DXT1 ? 8 : 16);
            rows = (h + 3) / 4;
        }
        tex->data_size += tex->level_pitches[level] * rows;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    tex->pitch = tex->level_pitches[0];
    if (tex->data_size < 128) tex->data_size = 128;
    tex->cpu_data = halo_vita_alloc(tex->data_size, 128, UINT32_MAX, PAGE_READWRITE);
    if (!tex->cpu_data) tex->cpu_data = malloc(tex->data_size);
    if (!tex->cpu_data) { free(tex); *ppTexture = NULL; return E_OUTOFMEMORY; }
    memset(tex->cpu_data, 0, tex->data_size);
    tex->base.Data = (DWORD)tex->cpu_data;

    tex->base.Format = ((DWORD)format << D3DFORMAT_FORMAT_SHIFT) | (2 << D3DFORMAT_DIMENSION_SHIFT);
    tex->base.Size = ((width - 1)) | ((height - 1) << 12);

    if (ppTexture) *ppTexture = (struct D3DTexture *)tex;
    return S_OK;
}

void WINAPI D3DTexture_LockRect(struct D3DTexture *texture, unsigned int level, struct _D3DLOCKED_RECT *locked_rect, const struct tagRECT *rect, unsigned long flags) {
    (void)level; (void)rect; (void)flags;
    if (locked_rect && texture) {
        locked_rect->pBits = NULL;
        locked_rect->Pitch = 0;
        struct d3d_texture_impl *impl = vita_texture_binding_get((struct D3DBaseTexture *)texture);
        if (!impl) return;
        if (texture->Common & D3DCOMMON_D3DCREATED) {
            if (level >= impl->levels) return;
            locked_rect->pBits = (unsigned char *)impl->cpu_data + impl->level_offsets[level];
            locked_rect->Pitch = impl->level_pitches[level];
        } else {
            if (level) return;
            locked_rect->pBits = (void *)texture->Data;
            locked_rect->Pitch = impl->pitch ? impl->pitch : impl->width * vita_format_bytes_per_pixel(impl->format);
        }
        impl->uploaded = 0;
        if (rect) {
            unsigned int width = impl->width >> level, height = impl->height >> level;
            if (!width) width = 1; if (!height) height = 1;
            if (rect->left < 0 || rect->top < 0 || rect->right <= rect->left || rect->bottom <= rect->top ||
                (unsigned int)rect->right > width || (unsigned int)rect->bottom > height) {
                locked_rect->pBits = NULL; return;
            }
            if (vita_is_dxt_format(impl->format)) {
                if ((rect->left & 3) || (rect->top & 3)) { locked_rect->pBits = NULL; return; }
                locked_rect->pBits = (unsigned char *)locked_rect->pBits + (rect->top / 4) * locked_rect->Pitch +
                    (rect->left / 4) * (impl->format == D3DFMT_DXT1 ? 8 : 16);
            } else if (vita_is_format_linear(impl->format, texture->Size)) {
                locked_rect->pBits = (unsigned char *)locked_rect->pBits + rect->top * locked_rect->Pitch +
                    rect->left * vita_format_bytes_per_pixel(impl->format);
            } else {
                /* A swizzled subrectangle cannot be represented by one linear pitch. */
                locked_rect->pBits = NULL;
            }
        }
    }
}

void WINAPI D3DTexture_GetLevelDesc(struct D3DTexture *texture, unsigned int level, struct _D3DSURFACE_DESC *desc) {
    (void)level;
    if (desc && texture) {
        struct d3d_texture_impl *impl = vita_texture_binding_get((struct D3DBaseTexture *)texture);
        memset(desc, 0, sizeof(*desc));
        if (!impl || level >= 13) return;
        desc->Width = (impl->width >> level) ? impl->width >> level : 1;
        desc->Height = (impl->height >> level) ? impl->height >> level : 1;
        desc->Format = impl->format;
        desc->Type = D3DRTYPE_TEXTURE;
    }
}

long WINAPI D3DTexture_GetSurfaceLevel(struct D3DTexture *texture, unsigned int level, struct D3DSurface **surface) {
    struct d3d_texture_impl *owner, *view;
    if (!surface) return E_INVALIDARG;
    *surface = NULL;
    if (!texture || !(texture->Common & D3DCOMMON_D3DCREATED)) return E_INVALIDARG;
    owner = vita_texture_binding_get((struct D3DBaseTexture *)texture);
    if (!owner || level >= owner->levels) return E_INVALIDARG;
    if (owner->surfaces[level]) {
        owner->surfaces[level]->base.Common++;
        *surface = (struct D3DSurface *)owner->surfaces[level];
        return S_OK;
    }
    view = (struct d3d_texture_impl *)calloc(1, sizeof(*view));
    if (!view) return E_OUTOFMEMORY;
    view->base.Common = D3DCOMMON_TYPE_SURFACE | D3DCOMMON_D3DCREATED | 1;
    view->base.Format = texture->Format;
    view->width = owner->width >> level;
    view->height = owner->height >> level;
    if (!view->width) view->width = 1;
    if (!view->height) view->height = 1;
    view->base.Size = (view->width - 1) | ((view->height - 1) << 12);
    view->format = owner->format;
    view->pitch = owner->level_pitches[level];
    view->cpu_data = (unsigned char *)owner->cpu_data + owner->level_offsets[level];
    view->base.Data = (DWORD)view->cpu_data;
    view->surface_owner = owner;
    view->surface_level = level;
    owner->surfaces[level] = view;
    texture->Common++;
    *surface = (struct D3DSurface *)view;
    return S_OK;
}

void WINAPI D3DDevice_SetTexture(unsigned long stage, struct D3DBaseTexture *texture) {
    static unsigned char upload_log_count[256];
    if (stage < D3DTSS_MAXSTAGES) {
        unsigned int gl_stage = stage < (unsigned long)g_ffp_texture_units ? stage : 0;
        g_texture_sources[stage] = texture;
        glActiveTexture(GL_TEXTURE0 + gl_stage);
        if (texture) {
            struct d3d_texture_impl *impl = vita_texture_binding_get(texture);
            g_textures[stage] = impl;
            if (!impl || !impl->cpu_data || impl->width > 2048 || impl->height > 2048) {
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_TEXTURE_2D);
                glActiveTexture(GL_TEXTURE0);
                return;
            }
            if (!impl->gl_id || !impl->uploaded) {
                if (!impl->gl_id) glGenTextures(1, &impl->gl_id);
                glBindTexture(GL_TEXTURE_2D, impl->gl_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

                /* SGX543 has no native S3TC support.  VitaGL may accept a
                 * compressed upload without reporting GL_INVALID_ENUM, but
                 * the resulting texture is undefined.  Decode every DXT
                 * resource on the CPU and upload one known RGBA layout. */
                unsigned long *rgba = (unsigned long *)malloc(impl->width * impl->height * 4);
                if (rgba) {
                    if (vita_is_dxt_format(impl->format)) {
                        vita_dxt_decode(impl->format, (const unsigned char *)impl->cpu_data,
                                        impl->width, impl->height, rgba);
                    } else {
                        unsigned long size = ((struct D3DTexture *)texture)->Size;
                        const unsigned long *palette = g_palettes[stage] ?
                            (const unsigned long *)g_palettes[stage]->Data : NULL;
                        vita_decode_uncompressed(impl, size, rgba, palette);
                    }
                    (void)glGetError();
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, impl->width, impl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                    {
                        GLenum upload_error = glGetError();
                        unsigned int format_index = (unsigned int)impl->format & 255;
                        impl->uploaded = upload_error == GL_NO_ERROR;
                        if (upload_log_count[format_index] < 2) {
                            unsigned int pixel_count = impl->width * impl->height;
                            unsigned int zero_alpha = 0, partial_alpha = 0, full_alpha = 0, i;
                            unsigned long hash = 2166136261UL;
                            FILE *log;
                            for (i = 0; i < pixel_count; ++i) {
                                unsigned int alpha = (rgba[i] >> 24) & 0xff;
                                if (!alpha) ++zero_alpha;
                                else if (alpha == 255) ++full_alpha;
                                else ++partial_alpha;
                            }
                            for (i = 0; i < impl->data_size; ++i)
                                hash = (hash ^ ((const unsigned char *)impl->cpu_data)[i]) * 16777619UL;
                            log = fopen("ux0:data/halo/boot.log", "a");
                            if (log) {
                                fprintf(log, "vita_texture: stage=%lu format=%u size=%ux%u bytes=%u dxt=%d palette=%d "
                                    "source_hash=%08lx alpha=%u/%u/%u gl_error=0x%x uploaded=%d\n",
                                    stage, format_index, impl->width, impl->height, impl->data_size,
                                    vita_is_dxt_format(impl->format), g_palettes[stage] != NULL, hash,
                                    zero_alpha, partial_alpha, full_alpha, (unsigned int)upload_error, impl->uploaded);
                                fclose(log);
                            }
                            ++upload_log_count[format_index];
                        }
                    }
                    free(rgba);
                }
            } else {
                glBindTexture(GL_TEXTURE_2D, impl->gl_id);
            }
            glEnable(GL_TEXTURE_2D);
        } else {
            g_textures[stage] = NULL;
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
        glActiveTexture(GL_TEXTURE0);
    }
}

/* ---------- Drawing */

static GLenum d3d_to_gl_prim(enum _D3DPRIMITIVETYPE pt) {
    switch (pt) {
        case D3DPT_POINTLIST: return GL_POINTS;
        case D3DPT_LINELIST: return GL_LINES;
        case D3DPT_LINESTRIP: return GL_LINE_STRIP;
        case D3DPT_TRIANGLELIST: return GL_TRIANGLES;
        case D3DPT_TRIANGLESTRIP: return GL_TRIANGLE_STRIP;
        case D3DPT_TRIANGLEFAN: return GL_TRIANGLE_FAN;
        default: return GL_TRIANGLES;
    }
}

void WINAPI D3DDevice_DrawVertices(enum _D3DPRIMITIVETYPE primitive_type, unsigned int start_vertex, unsigned int vertex_count) {
    if (!g_streams[0] || !g_streams[0]->Data || !vertex_count) return;
    unsigned int stride = g_stream_strides[0] ? g_stream_strides[0] : 32;
    const unsigned char *base = (const unsigned char *)g_streams[0]->Data;

    vita_apply_render_states();

    glMatrixMode(GL_PROJECTION);
    if (g_vsh_constants[0][0] != 0.0f || g_vsh_constants[3][3] != 0.0f) {
        glLoadMatrixf((const GLfloat *)&g_vsh_constants[0]);
    } else {
        glLoadIdentity();
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    if (g_textures[0] && g_textures[0]->gl_id) {
        glBindTexture(GL_TEXTURE_2D, g_textures[0]->gl_id);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, (GLsizei)stride, base);
    if (stride >= 24) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, (GLsizei)stride, base + 12);
    }
    if (stride >= 32) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, (GLsizei)stride, base + (stride - 8));
    }
    vita_prepare_texture(stride >= 32 ? base + (stride - 8) : NULL, (GLsizei)stride);
    vita_apply_vertex_declaration(0);
    if (vita_draw_combiner(primitive_type, vertex_count, start_vertex, NULL, base, stride, 0)) {
        /* shader path drew the geometry; client state cleanup remains below */
    } else if (primitive_type == D3DPT_QUADLIST) {
        unsigned int first;
        for (first = 0; first + 3 < vertex_count; first += 4)
            glDrawArrays(GL_TRIANGLE_FAN, start_vertex + first, 4);
    } else glDrawArrays(d3d_to_gl_prim(primitive_type), (GLint)start_vertex, (GLsizei)vertex_count);
    if (stride >= 32) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (stride >= 24) glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void WINAPI D3DDevice_DrawIndexedVertices(enum _D3DPRIMITIVETYPE primitive_type, unsigned int vertex_count, const unsigned short *index_data) {
    if (!g_streams[0] || !g_streams[0]->Data || !vertex_count) return;
    unsigned int stride = g_stream_strides[0] ? g_stream_strides[0] : 32;
    const unsigned char *base = (const unsigned char *)g_streams[0]->Data + g_base_vertex * stride;

    vita_apply_render_states();

    glMatrixMode(GL_PROJECTION);
    if (g_vsh_constants[0][0] != 0.0f || g_vsh_constants[3][3] != 0.0f) {
        glLoadMatrixf((const GLfloat *)&g_vsh_constants[0]);
    } else {
        glLoadIdentity();
    }
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glActiveTexture(GL_TEXTURE0);
    glClientActiveTexture(GL_TEXTURE0);
    if (g_textures[0] && g_textures[0]->gl_id) {
        glBindTexture(GL_TEXTURE_2D, g_textures[0]->gl_id);
        glEnable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
    }

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, (GLsizei)stride, base);
    if (stride >= 24) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, (GLsizei)stride, base + 12);
    }
    if (stride >= 32) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, (GLsizei)stride, base + (stride - 8));
    }
    const unsigned short *indices = index_data ? index_data : D3D__IndexData;
    if (indices) {
        vita_prepare_texture(stride >= 32 ? base + (stride - 8) : NULL, (GLsizei)stride);
        vita_apply_vertex_declaration(g_base_vertex);
        if (vita_draw_combiner(primitive_type, vertex_count, 0, indices, base, stride, g_base_vertex)) {
            /* shader path drew the geometry */
        } else if (primitive_type == D3DPT_QUADLIST) {
            unsigned int first;
            for (first = 0; first + 3 < vertex_count; first += 4)
                glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, indices + first);
        } else glDrawElements(d3d_to_gl_prim(primitive_type), (GLsizei)vertex_count, GL_UNSIGNED_SHORT, indices);
    }
    if (stride >= 32) glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    if (stride >= 24) glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

/* ---------- Resource Management */

void WINAPI D3DResource_Register(struct D3DResource *resource, void *base) {
    if (!resource) return;
    DWORD *fields = (DWORD *)resource;
    fields[1] = (DWORD)base + fields[1];
}

unsigned long WINAPI D3DResource_Release(struct D3DResource *resource) {
    if (!resource) return 0;
    DWORD *fields = (DWORD *)resource;
    ULONG count = fields[0] & D3DCOMMON_REFCOUNT_MASK;
    if (count) {
        count--;
        fields[0] = (fields[0] & ~D3DCOMMON_REFCOUNT_MASK) | count;
    }
    if (!count && (fields[0] & D3DCOMMON_D3DCREATED)) {
        if ((fields[0] & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_SURFACE) {
            struct d3d_texture_impl *view = (struct d3d_texture_impl *)resource;
            if (g_render_texture == view) D3DDevice_SetRenderTarget(&g_back_buffer, NULL);
            if (view->framebuffer) glDeleteFramebuffers(1, &view->framebuffer);
            if (view->depthbuffer) glDeleteRenderbuffers(1, &view->depthbuffer);
            if (view->surface_level && view->gl_id) glDeleteTextures(1, &view->gl_id);
            if (view->surface_owner) {
                view->surface_owner->surfaces[view->surface_level] = NULL;
                D3DResource_Release((struct D3DResource *)view->surface_owner);
            }
            free(view);
            return 0;
        }
        unsigned int i;
        unsigned long type = fields[0] & D3DCOMMON_TYPE_MASK;
        int embedded = type != D3DCOMMON_TYPE_VERTEXBUFFER && type != D3DCOMMON_TYPE_INDEXBUFFER;
        if ((fields[0] & D3DCOMMON_TYPE_MASK) == D3DCOMMON_TYPE_TEXTURE) {
            struct D3DTexture *header = (struct D3DTexture *)resource;
            embedded = ((header->Format >> D3DFORMAT_DIMENSION_SHIFT) & 15) != 2;
            if (!embedded) {
                struct d3d_texture_impl *impl = (struct d3d_texture_impl *)resource;
                if (g_render_texture == impl) D3DDevice_SetRenderTarget(&g_back_buffer, NULL);
                for (i = 0; i < D3DTSS_MAXSTAGES; ++i)
                    if (g_textures[i] == impl) g_textures[i] = NULL;
                if (impl->framebuffer) glDeleteFramebuffers(1, &impl->framebuffer);
                if (impl->depthbuffer) glDeleteRenderbuffers(1, &impl->depthbuffer);
                if (impl->gl_id) glDeleteTextures(1, &impl->gl_id);
            }
        }
        for (i = 0; i < 16; ++i)
            if ((void *)g_streams[i] == (void *)resource) g_streams[i] = NULL;
        if ((void *)g_current_ib == (void *)resource) { g_current_ib = NULL; D3D__IndexData = NULL; }
        if (fields[1] && !embedded && halo_vita_free((void *)fields[1]) < 0)
            free((void *)fields[1]);
        free(resource);
    }
    return count;
}

int WINAPI D3DResource_IsBusy(struct D3DResource *resource) { (void)resource; return 0; }
void WINAPI D3DResource_BlockUntilNotBusy(struct D3DResource *resource) { (void)resource; }

/* ---------- Render States */

static GLenum d3d_to_gl_blend(unsigned long blend) {
    switch (blend) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    /* GXM/VitaGL exposes no constant blend factor or glBlendColor. For Halo's
     * screen-flash passes the pixel shader emits the same tint stored in
     * BLENDCOLOR, so source-color factors are the closest representable
     * equivalent and avoid the previous (incorrect) unconditional ONE. */
    case D3DBLEND_CONSTANTCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVCONSTANTCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_CONSTANTALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVCONSTANTALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    default: return GL_ONE;
    }
}

static GLenum d3d_to_gl_cmp(unsigned long cmp) {
    switch (cmp) {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    case D3DCMP_ALWAYS: return GL_ALWAYS;
    default: return GL_ALWAYS;
    }
}

static void vita_apply_blend_state(void) {
    if (D3D__RenderState[D3DRS_ALPHABLENDENABLE]) {
        /* ZERO is a real Xbox blend factor, not an unset state. */
        glEnable(GL_BLEND);
        glBlendFunc(d3d_to_gl_blend(D3D__RenderState[D3DRS_SRCBLEND]),
                    d3d_to_gl_blend(D3D__RenderState[D3DRS_DESTBLEND]));
        switch (D3D__RenderState[D3DRS_BLENDOP]) {
        case D3DBLENDOP_SUBTRACT: glBlendEquation(GL_FUNC_SUBTRACT); break;
        case D3DBLENDOP_REVSUBTRACT: glBlendEquation(GL_FUNC_REVERSE_SUBTRACT); break;
        case D3DBLENDOP_MIN: glBlendEquation(GL_MIN); break;
        case D3DBLENDOP_MAX: glBlendEquation(GL_MAX); break;
        default: glBlendEquation(GL_FUNC_ADD); break;
        }
    } else {
        glDisable(GL_BLEND);
    }
}

static void vita_apply_render_states(void) {
    vita_apply_blend_state();

    if (D3D__RenderState[D3DRS_ALPHATESTENABLE]) {
        unsigned long func_rs = D3D__RenderState[D3DRS_ALPHAFUNC] ? D3D__RenderState[D3DRS_ALPHAFUNC] : D3DCMP_GREATER;
        float ref = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff) / 255.0f;
        glEnable(GL_ALPHA_TEST);
        glAlphaFunc(d3d_to_gl_cmp(func_rs), ref);
    } else {
        glDisable(GL_ALPHA_TEST);
    }

    if (D3D__RenderState[D3DRS_ZENABLE]) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }

    glDepthMask(D3D__RenderState[D3DRS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);

    if (D3D__RenderState[D3DRS_ZFUNC]) {
        glDepthFunc(d3d_to_gl_cmp(D3D__RenderState[D3DRS_ZFUNC]));
    }

    if (D3D__RenderState[D3DRS_COLORWRITEENABLE]) {
        unsigned long cw = D3D__RenderState[D3DRS_COLORWRITEENABLE];
        GLboolean r = (cw & D3DCOLORWRITEENABLE_RED) != 0;
        GLboolean g = (cw & D3DCOLORWRITEENABLE_GREEN) != 0;
        GLboolean b = (cw & D3DCOLORWRITEENABLE_BLUE) != 0;
        GLboolean a = (cw & D3DCOLORWRITEENABLE_ALPHA) != 0;
        glColorMask(r, g, b, a);
    } else {
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    }

    if (D3D__RenderState[D3DRS_CULLMODE] == D3DCULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else if (D3D__RenderState[D3DRS_CULLMODE] == D3DCULL_CW || D3D__RenderState[D3DRS_CULLMODE] == D3DCULL_CCW) {
        glEnable(GL_CULL_FACE);
        glCullFace(D3D__RenderState[D3DRS_CULLMODE] == D3DCULL_CW ? GL_FRONT : GL_BACK);
    }
}

void WINAPI D3DDevice_SetRenderStateNotInline(enum _D3DRENDERSTATETYPE state, unsigned long value) {
    /* HALO_VITA's inline dispatcher routes ALL states here, including the
     * texture modes used by the short text/UI pixel shaders. */
    if (state == D3DRS_PSTEXTUREMODES) g_texture_modes = value;
    if ((unsigned long)state < D3DRS_MAX) {
        D3D__RenderState[state] = value;
    }
    if (state == D3DRS_ZENABLE) {
        if (value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    } else if (state == D3DRS_ZWRITEENABLE) {
        glDepthMask(value ? GL_TRUE : GL_FALSE);
    } else if (state == D3DRS_ZFUNC) {
        glDepthFunc(d3d_to_gl_cmp(value));
    } else if (state == D3DRS_ALPHABLENDENABLE || state == D3DRS_SRCBLEND || state == D3DRS_DESTBLEND ||
               state == D3DRS_BLENDOP) {
        vita_apply_blend_state();
    } else if (state == D3DRS_ALPHATESTENABLE || state == D3DRS_ALPHAREF || state == D3DRS_ALPHAFUNC) {
        if (D3D__RenderState[D3DRS_ALPHATESTENABLE]) {
            GLenum func = d3d_to_gl_cmp(D3D__RenderState[D3DRS_ALPHAFUNC] ? D3D__RenderState[D3DRS_ALPHAFUNC] : D3DCMP_GREATER);
            float ref = (float)(D3D__RenderState[D3DRS_ALPHAREF] & 0xff) / 255.0f;
            glEnable(GL_ALPHA_TEST);
            glAlphaFunc(func, ref);
        } else {
            glDisable(GL_ALPHA_TEST);
        }
    } else if (state == D3DRS_COLORWRITEENABLE) {
        GLboolean r = (value & 0x00010000) || (value & 0x01) ? GL_TRUE : GL_FALSE;
        GLboolean g = (value & 0x00000100) || (value & 0x02) ? GL_TRUE : GL_FALSE;
        GLboolean b = (value & 0x00000001) || (value & 0x04) ? GL_TRUE : GL_FALSE;
        GLboolean a = (value & 0x01000000) || (value & 0x08) ? GL_TRUE : GL_FALSE;
        glColorMask(r, g, b, a);
    } else if (state == D3DRS_CULLMODE) {
        if (value == D3DCULL_NONE) glDisable(GL_CULL_FACE);
        else {
            glEnable(GL_CULL_FACE);
            glCullFace(value == D3DCULL_CW ? GL_FRONT : GL_BACK);
        }
    }
}

void WINAPI D3DDevice_SetRenderState_ZBias(unsigned long value) {
    float offset = -(float)value;
    float slope = offset * 0.25f;
    if (value != 0) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(slope, offset);
    } else {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    D3D__RenderState[D3DRS_ZBIAS] = value;
}

void __fastcall D3DDevice_SetRenderState_Simple(unsigned long method, unsigned long value) {
    (void)method; (void)value;
}

void __fastcall D3DDevice_SetRenderState_Deferred(enum _D3DRENDERSTATETYPE state, unsigned long value) {
    if (state == D3DRS_PSTEXTUREMODES) g_texture_modes = value;
    if ((unsigned long)state < D3DRS_MAX) D3D__RenderState[state] = value;
}

#define DEFINE_RENDER_STATE_STUB(name, rs) \
    void WINAPI D3DDevice_SetRenderState_##name(unsigned long value) { \
        D3DDevice_SetRenderStateNotInline(rs, value); \
    }

DEFINE_RENDER_STATE_STUB(BackFillMode, D3DRS_BACKFILLMODE)
DEFINE_RENDER_STATE_STUB(CullMode, D3DRS_CULLMODE)
DEFINE_RENDER_STATE_STUB(DoNotCullUncompressed, D3DRS_DONOTCULLUNCOMPRESSED)
DEFINE_RENDER_STATE_STUB(Dxt1NoiseEnable, D3DRS_DXT1NOISEENABLE)
DEFINE_RENDER_STATE_STUB(EdgeAntiAlias, D3DRS_EDGEANTIALIAS)
DEFINE_RENDER_STATE_STUB(FillMode, D3DRS_FILLMODE)
DEFINE_RENDER_STATE_STUB(FogColor, D3DRS_FOGCOLOR)
DEFINE_RENDER_STATE_STUB(FrontFace, D3DRS_FRONTFACE)
DEFINE_RENDER_STATE_STUB(LineWidth, D3DRS_LINEWIDTH)
DEFINE_RENDER_STATE_STUB(LogicOp, D3DRS_LOGICOP)
DEFINE_RENDER_STATE_STUB(MultiSampleAntiAlias, D3DRS_MULTISAMPLEANTIALIAS)
DEFINE_RENDER_STATE_STUB(MultiSampleMask, D3DRS_MULTISAMPLEMASK)
DEFINE_RENDER_STATE_STUB(MultiSampleType, D3DRS_MULTISAMPLETYPE)
DEFINE_RENDER_STATE_STUB(NormalizeNormals, D3DRS_NORMALIZENORMALS)
DEFINE_RENDER_STATE_STUB(OcclusionCullEnable, D3DRS_OCCLUSIONCULLENABLE)
void WINAPI D3DDevice_SetRenderState_PSTextureModes(unsigned long value) {
    g_texture_modes = value;
    D3D__RenderState[D3DRS_PSTEXTUREMODES] = value;
}
DEFINE_RENDER_STATE_STUB(RopZCmpAlwaysRead, D3DRS_ROPZCMPALWAYSREAD)
DEFINE_RENDER_STATE_STUB(RopZRead, D3DRS_ROPZREAD)
DEFINE_RENDER_STATE_STUB(ShadowFunc, D3DRS_SHADOWFUNC)
DEFINE_RENDER_STATE_STUB(StencilCullEnable, D3DRS_STENCILCULLENABLE)
DEFINE_RENDER_STATE_STUB(StencilEnable, D3DRS_STENCILENABLE)
DEFINE_RENDER_STATE_STUB(StencilFail, D3DRS_STENCILFAIL)
DEFINE_RENDER_STATE_STUB(TextureFactor, D3DRS_TEXTUREFACTOR)
DEFINE_RENDER_STATE_STUB(TwoSidedLighting, D3DRS_TWOSIDEDLIGHTING)
DEFINE_RENDER_STATE_STUB(VertexBlend, D3DRS_VERTEXBLEND)
DEFINE_RENDER_STATE_STUB(YuvEnable, D3DRS_YUVENABLE)
DEFINE_RENDER_STATE_STUB(ZEnable, D3DRS_ZENABLE)

/* ---------- Texture States */

void __fastcall D3DDevice_SetTextureState_Deferred(unsigned long stage, enum _D3DTEXTURESTAGESTATETYPE type, unsigned long value) {
    if (stage < 4 && (unsigned long)type < 32) D3D__TextureState[stage][type] = value;
}
void WINAPI D3DDevice_SetTextureState_BorderColor(unsigned long stage, unsigned long color) { (void)stage; (void)color; }
void WINAPI D3DDevice_SetTextureState_BumpEnv(unsigned long stage, enum _D3DTEXTURESTAGESTATETYPE type, unsigned long value) {
    if (stage < D3DTSS_MAXSTAGES && (unsigned int)type < 32) D3D__TextureState[stage][type] = value;
}
void WINAPI D3DDevice_SetTextureState_ColorKeyColor(unsigned long stage, unsigned long color) { (void)stage; (void)color; }
void WINAPI D3DDevice_SetTextureState_TexCoordIndex(unsigned long stage, unsigned long index) { (void)stage; (void)index; }

/* ---------- Immediate Vertex Data */

static void immediate_set_attribute(int register_index, float x, float y, float z, float w) {
    int emit = register_index == D3DVSDE_VERTEX || register_index == D3DVSDE_POSITION;
    int index = register_index == D3DVSDE_VERTEX ? D3DVSDE_POSITION : register_index;
    if (index < 0 || index >= 16) return;
    g_immediate_attributes[index][0] = x;
    g_immediate_attributes[index][1] = y;
    g_immediate_attributes[index][2] = z;
    g_immediate_attributes[index][3] = w;
    if (emit) immediate_emit(x, y, z, w);
}

void WINAPI D3DDevice_SetVertexData2f(int register_index, float x, float y) {
    if (register_index == D3DVSDE_SPECULAR || register_index >= D3DVSDE_TEXCOORD0)
        g_immediate_texcoord_register = register_index;
    immediate_set_attribute(register_index, x, y, 0.0f, 1.0f);
}
void WINAPI D3DDevice_SetVertexData2s(int register_index, short x, short y) {
    if (register_index == D3DVSDE_SPECULAR || register_index >= D3DVSDE_TEXCOORD0)
        g_immediate_texcoord_register = register_index;
    immediate_set_attribute(register_index, (float)x, (float)y, 0.0f, 1.0f);
}
void WINAPI D3DDevice_SetVertexData4f(int register_index, float x, float y, float z, float w) {
    if (register_index == D3DVSDE_DIFFUSE)
        g_immediate_color_register = register_index;
    immediate_set_attribute(register_index, x, y, z, w);
}
void WINAPI D3DDevice_SetVertexData4ub(int register_index, unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3) {
    g_immediate_color_register = register_index;
    immediate_set_attribute(register_index, b0 / 255.0f, b1 / 255.0f, b2 / 255.0f, b3 / 255.0f);
}
void WINAPI D3DDevice_SetVertexDataColor(int register_index, unsigned long color) {
    g_immediate_color_register = register_index;
    immediate_set_attribute(register_index,
        ((color >> 16) & 0xff) / 255.0f,
        ((color >> 8) & 0xff) / 255.0f,
        (color & 0xff) / 255.0f,
        ((color >> 24) & 0xff) / 255.0f);
}

/* ---------- Shaders & Capabilities */

long WINAPI D3DDevice_CreateVertexShader(const unsigned long *declaration, const unsigned long *function, unsigned long *shader_handle, unsigned long usage) {
    unsigned int slot, stream = 0, offsets[16] = {0}, tokens = 0;
    struct vita_vertex_shader *shader;
    (void)usage;
    if (!shader_handle) return E_INVALIDARG;
    *shader_handle = 0;
    for (slot = 1; slot < 512 && g_vertex_shaders[slot]; ++slot) {}
    if (slot == 512) return E_OUTOFMEMORY;
    shader = calloc(1, sizeof(*shader));
    if (!shader) return E_OUTOFMEMORY;
    shader->instruction_count = function ? function[0] >> 16 : 0;
    if (shader->instruction_count > 136) { free(shader); return E_INVALIDARG; }
    if (shader->instruction_count) {
        shader->instructions = malloc(shader->instruction_count * 4 * sizeof(DWORD));
        if (!shader->instructions) { free(shader); return E_OUTOFMEMORY; }
        memcpy(shader->instructions, function + 1, shader->instruction_count * 4 * sizeof(DWORD));
    }
    shader->identity = ++g_vertex_shader_identity;
    while (declaration && *declaration != D3DVSD_END() && tokens++ < 256) {
        unsigned long token = *declaration++;
        unsigned int kind = token >> 29;
        if (kind == 1) stream = token & 15;
        else if (kind == 2) {
            if (token & D3DVSD_DATALOADTYPEMASK) {
                unsigned int count = (token >> 16) & 15;
                offsets[stream] += (token & 0x08000000) ? count : count * 4;
            } else {
                unsigned int reg = token & 31, type = (token >> 16) & 255;
                unsigned int count = type >> 4, encoding = type & 15;
                unsigned int bytes;
                if (count == 7) count = 3;
                bytes = encoding == 6 ? 4 : count * (encoding == 2 ? 4 : (encoding == 1 || encoding == 5 ? 2 : 1));
                if (reg < 16) {
                    shader->elements[reg].stream = stream; shader->elements[reg].offset = offsets[stream];
                    shader->elements[reg].type = type; shader->elements[reg].present = 1;
                }
                offsets[stream] += bytes;
            }
        } else if (kind == 4) declaration += ((token >> 25) & 15) * 4;
        else if (kind == 5) declaration += (token >> 24) & 31;
    }
    g_vertex_shaders[slot] = shader;
    *shader_handle = slot * 2;
    return S_OK;
}
void WINAPI D3DDevice_DeleteVertexShader(unsigned long shader_handle) {
    unsigned int slot = shader_handle / 2;
    unsigned int address;
    if ((shader_handle & 1) || !slot || slot >= 512) return;
    if (g_vertex_declaration == g_vertex_shaders[slot]) g_vertex_declaration = NULL;
    for (address = 0; address < 136; ++address)
        if (g_vertex_program_slots[address] == g_vertex_shaders[slot]) g_vertex_program_slots[address] = NULL;
    if (g_vertex_shaders[slot]) free(g_vertex_shaders[slot]->instructions);
    free(g_vertex_shaders[slot]); g_vertex_shaders[slot] = NULL;
}
void WINAPI D3DDevice_LoadVertexShader(unsigned long shader_handle, unsigned long address) {
    if (address < 136 && !(shader_handle & 1) && shader_handle / 2 < 512)
        g_vertex_program_slots[address] = g_vertex_shaders[shader_handle / 2];
}
void WINAPI D3DDevice_SelectVertexShader(unsigned long shader_handle, unsigned long address) {
    if (address < 136) g_vertex_program_address = address;
    if (!(shader_handle & 1) && shader_handle / 2 < 512 && g_vertex_shaders[shader_handle / 2])
        g_vertex_declaration = g_vertex_shaders[shader_handle / 2];
}
void WINAPI D3DDevice_GetVertexShaderSize(unsigned long shader_handle, unsigned int *size) {
    if (size) *size = !(shader_handle & 1) && shader_handle / 2 < 512 && g_vertex_shaders[shader_handle / 2] ?
        g_vertex_shaders[shader_handle / 2]->instruction_count : 0;
}
void WINAPI D3DDevice_SetVertexShader(unsigned long shader_handle) {
    g_vertex_declaration = !(shader_handle & 1) && shader_handle / 2 < 512 ? g_vertex_shaders[shader_handle / 2] : NULL;
    g_vertex_program_slots[0] = g_vertex_declaration;
    g_vertex_program_address = 0;
}
void WINAPI D3DDevice_SetVertexShaderConstant(int register_index, const void *constant_data, unsigned long constant_count) {
    long first = register_index + 96; /* XGPU_VERTEX_CONSTANT_BIAS */
    if (first >= 0 && first < 192 && constant_data && constant_count) {
        if (constant_count > (unsigned long)(192 - first)) constant_count = 192 - first;
        memcpy(&g_vsh_constants[first], constant_data, constant_count * 4 * sizeof(float));
    }
}
void WINAPI D3DDevice_SetShaderConstantMode(unsigned long mode) { g_shader_constant_mode = mode; vita_update_viewport_constants(); }
void WINAPI D3DDevice_SetPixelShaderProgram(struct _D3DPixelShaderDef *program) {
    static unsigned int log_count;
    if (!program) return;

    /* On Xbox these fields are the register-combiner state, not merely a
     * texture-stage hint. Keep the complete definition in the same render
     * state shadow consumed by the Linux/Android shader translator. The Vita
     * fixed-function path cannot execute all combiners yet, but discarding
     * them here also breaks diagnostics and any translated fallback. */
    memcpy(&D3D__RenderState[D3DRS_PSALPHAINPUTS0], program->PSAlphaInputs, sizeof(program->PSAlphaInputs));
    D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSABCD] = program->PSFinalCombinerInputsABCD;
    D3D__RenderState[D3DRS_PSFINALCOMBINERINPUTSEFG] = program->PSFinalCombinerInputsEFG;
    memcpy(&D3D__RenderState[D3DRS_PSCONSTANT0_0], program->PSConstant0, sizeof(program->PSConstant0));
    memcpy(&D3D__RenderState[D3DRS_PSCONSTANT1_0], program->PSConstant1, sizeof(program->PSConstant1));
    memcpy(&D3D__RenderState[D3DRS_PSALPHAOUTPUTS0], program->PSAlphaOutputs, sizeof(program->PSAlphaOutputs));
    memcpy(&D3D__RenderState[D3DRS_PSRGBINPUTS0], program->PSRGBInputs, sizeof(program->PSRGBInputs));
    D3D__RenderState[D3DRS_PSCOMPAREMODE] = program->PSCompareMode;
    D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT0] = program->PSFinalCombinerConstant0;
    D3D__RenderState[D3DRS_PSFINALCOMBINERCONSTANT1] = program->PSFinalCombinerConstant1;
    memcpy(&D3D__RenderState[D3DRS_PSRGBOUTPUTS0], program->PSRGBOutputs, sizeof(program->PSRGBOutputs));
    D3D__RenderState[D3DRS_PSCOMBINERCOUNT] = program->PSCombinerCount;
    D3D__RenderState[D3DRS_PSTEXTUREMODES] = program->PSTextureModes;
    D3D__RenderState[D3DRS_PSDOTMAPPING] = program->PSDotMapping;
    D3D__RenderState[D3DRS_PSINPUTTEXTURE] = program->PSInputTexture;
    g_texture_modes = program->PSTextureModes;
    if (log_count < 32) {
        FILE *log = fopen("ux0:data/halo/boot.log", "a");
        if (log) {
            fprintf(log, "vita_pixel_shader: modes=%08lx combiners=%lu rgb0=%08lx/%08lx alpha0=%08lx/%08lx final=%08lx/%08lx\n",
                program->PSTextureModes, program->PSCombinerCount,
                program->PSRGBInputs[0], program->PSRGBOutputs[0],
                program->PSAlphaInputs[0], program->PSAlphaOutputs[0],
                program->PSFinalCombinerInputsABCD, program->PSFinalCombinerInputsEFG);
            fclose(log);
        }
        ++log_count;
    }
}
void WINAPI D3DDevice_SetFlickerFilter(unsigned long filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(int enable) { (void)enable; }
void WINAPI D3DDevice_SetVerticalBlankCallback(void (*callback)(unsigned long)) {
    g_vblank_callback = callback;
    FILE *log = fopen("ux0:data/halo/boot.log", "a");
    if (log) {
        fprintf(log, "d3d8: SetVerticalBlankCallback registered: %p\n", (void *)callback);
        fclose(log);
    }
}
void WINAPI D3DDevice_BlockUntilVerticalBlank(void) { sceDisplayWaitVblankStart(); }
void WINAPI D3DDevice_InsertCallback(enum _D3DCALLBACKTYPE type, void (*callback)(unsigned long), unsigned long context) { (void)type; (void)callback; (void)context; }
int WINAPI D3DDevice_IsBusy(void) { return 0; }
void WINAPI D3DDevice_KickPushBuffer(void) { }
void WINAPI D3DDevice_GetDeviceCaps(struct _D3DCAPS8 *caps) {
    if (!caps) return;
    memset(caps, 0, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->MaxTextureWidth = 2048;
    caps->MaxTextureHeight = 2048;
    caps->MaxTextureRepeat = 2048;
    caps->MaxTextureAspectRatio = 2048;
    caps->MaxSimultaneousTextures = 4;
    caps->VertexShaderVersion = D3DVS_VERSION(1, 1);
    caps->PixelShaderVersion = D3DPS_VERSION(1, 1);
}
void WINAPI D3DDevice_BeginVisibilityTest(void) { }
long WINAPI D3DDevice_EndVisibilityTest(unsigned long handle) { (void)handle; return S_OK; }
long WINAPI D3DDevice_GetVisibilityTestResult(unsigned long handle, unsigned int *samples, unsigned __int64 *timestamp) {
    (void)handle;
    if (samples) *samples = 1;
    if (timestamp) *timestamp = 0;
    return S_OK;
}
static unsigned int vita_mip_bytes(enum _D3DFORMAT format, unsigned int w, unsigned int h, unsigned int d) {
    if (vita_is_dxt_format(format)) return ((w + 3) / 4) * ((h + 3) / 4) * d * (format == D3DFMT_DXT1 ? 8 : 16);
    return w * h * d * vita_format_bytes_per_pixel(format);
}
static unsigned int vita_mip_dimension(unsigned int size, unsigned int level) {
    unsigned int value = size >> level;
    return value ? value : 1;
}
struct d3d_cube_texture_impl {
    struct D3DCubeTexture base;
    unsigned int edge, face_size, levels;
    unsigned int offsets[13];
    enum _D3DFORMAT format;
    unsigned char pixels[1];
};
struct d3d_volume_texture_impl {
    struct D3DVolumeTexture base;
    unsigned int width, height, depth, bpp, levels;
    unsigned int offsets[13];
    enum _D3DFORMAT format;
    unsigned char pixels[1];
};
long WINAPI D3DDevice_CreateCubeTexture(unsigned int edge, unsigned int levels, unsigned long usage, enum _D3DFORMAT format, unsigned long pool, struct D3DCubeTexture **ppCube) {
    struct d3d_cube_texture_impl *cube;
    unsigned int bpp = 4;
    unsigned int face_size = 0, offsets[13], max_levels = 1, n, extent = edge;
    (void)levels; (void)usage; (void)pool;
    if (!ppCube || !edge || edge > 2048) return E_INVALIDARG;
    *ppCube = NULL;
    while (extent > 1) { extent >>= 1; ++max_levels; }
    if (!levels) levels = max_levels;
    if (levels > max_levels) return E_INVALIDARG;
    if (format == D3DFMT_R5G6B5 || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4)
        bpp = 2;
    for (n = 0; n < levels; ++n) {
        offsets[n] = face_size;
        unsigned int side = vita_mip_dimension(edge, n);
        face_size += vita_mip_bytes(format, side, side, 1);
    }
    cube = (struct d3d_cube_texture_impl *)calloc(1, sizeof(*cube) - 1 + face_size * 6);
    if (!cube) return E_OUTOFMEMORY;
    cube->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    cube->base.Data = (DWORD)cube->pixels;
    cube->base.Format = ((DWORD)format << D3DFORMAT_FORMAT_SHIFT) | (3 << D3DFORMAT_DIMENSION_SHIFT);
    cube->base.Size = ((edge - 1)) | ((edge - 1) << 12);
    cube->edge = edge;
    cube->face_size = face_size;
    cube->levels = levels;
    cube->format = format;
    memcpy(cube->offsets, offsets, levels * sizeof(unsigned int));
    *ppCube = &cube->base;
    return S_OK;
}
void WINAPI D3DCubeTexture_LockRect(struct D3DCubeTexture *cube, enum _D3DCUBEMAP_FACES face, unsigned int level, struct _D3DLOCKED_RECT *rect, const struct tagRECT *r, unsigned long flags) {
    struct d3d_cube_texture_impl *impl = (struct d3d_cube_texture_impl *)cube;
    (void)level; (void)r; (void)flags;
    if (rect) {
        rect->Pitch = 0; rect->pBits = NULL;
        if (!impl || level >= impl->levels || (unsigned int)face >= 6 || r) return;
        unsigned int side = vita_mip_dimension(impl->edge, level);
        rect->Pitch = vita_mip_bytes(impl->format, side, 1, 1);
        rect->pBits = impl->pixels + (unsigned int)face * impl->face_size + impl->offsets[level];
    }
}
long WINAPI D3DDevice_CreateVolumeTexture(unsigned int w, unsigned int h, unsigned int d, unsigned int l, unsigned long u, enum _D3DFORMAT f, unsigned long p, struct D3DVolumeTexture **ppVol) {
    struct d3d_volume_texture_impl *volume;
    unsigned int bpp = 4;
    unsigned int data_size = 0, offsets[13], n, max_levels = 1, extent;
    (void)l; (void)u; (void)p;
    if (!ppVol || !w || !h || !d || w > 512 || h > 512 || d > 512) return E_INVALIDARG;
    *ppVol = NULL;
    extent = w > h ? w : h; if (d > extent) extent = d;
    while (extent > 1) { extent >>= 1; ++max_levels; }
    if (!l) l = max_levels;
    if (l > max_levels) return E_INVALIDARG;
    if (f == D3DFMT_R5G6B5 || f == D3DFMT_A1R5G5B5 || f == D3DFMT_A4R4G4B4)
        bpp = 2;
    bpp = vita_format_bytes_per_pixel(f);
    for (n = 0; n < l; ++n) {
        offsets[n] = data_size;
        data_size += vita_mip_bytes(f, vita_mip_dimension(w, n), vita_mip_dimension(h, n), vita_mip_dimension(d, n));
    }
    volume = (struct d3d_volume_texture_impl *)calloc(1, sizeof(*volume) - 1 + data_size);
    if (!volume) return E_OUTOFMEMORY;
    volume->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    volume->base.Data = (DWORD)volume->pixels;
    volume->base.Format = ((DWORD)f << D3DFORMAT_FORMAT_SHIFT) | (3 << D3DFORMAT_DIMENSION_SHIFT);
    volume->base.Size = ((w - 1)) | ((h - 1) << 12);
    volume->width = w; volume->height = h; volume->depth = d; volume->bpp = bpp;
    volume->levels = l; volume->format = f;
    memcpy(volume->offsets, offsets, l * sizeof(unsigned int));
    *ppVol = &volume->base;
    return S_OK;
}
void WINAPI D3DVolumeTexture_LockBox(struct D3DVolumeTexture *vol, unsigned int level, struct _D3DLOCKED_BOX *box, const struct _D3DBOX *b, unsigned long flags) {
    struct d3d_volume_texture_impl *impl = (struct d3d_volume_texture_impl *)vol;
    (void)level; (void)b; (void)flags;
    if (box) {
        box->RowPitch = box->SlicePitch = 0; box->pBits = NULL;
        if (!impl || level >= impl->levels || b) return;
        unsigned int w = vita_mip_dimension(impl->width, level), h = vita_mip_dimension(impl->height, level);
        box->RowPitch = vita_mip_bytes(impl->format, w, 1, 1);
        box->SlicePitch = vita_mip_bytes(impl->format, w, h, 1);
        box->pBits = impl->pixels + impl->offsets[level];
    }
}
long WINAPI D3DDevice_CreatePalette(enum _D3DPALETTESIZE size, struct D3DPalette **ppPalette) {
    struct d3d_palette_impl {
        struct D3DPalette base;
        unsigned int entry_count;
        unsigned long colors[256];
    };
    struct d3d_palette_impl *palette;
    unsigned int entry_count;

    if (!ppPalette || (unsigned int)size >= D3DPALETTE_MAX)
        return E_INVALIDARG;

    entry_count = 256u >> (unsigned int)size;
    palette = (struct d3d_palette_impl *)calloc(1, sizeof(*palette));
    if (!palette)
        return E_OUTOFMEMORY;

    palette->base.Common = D3DCOMMON_TYPE_PALETTE | D3DCOMMON_D3DCREATED | 1;
    palette->base.Data = (DWORD)palette->colors;
    palette->entry_count = entry_count;
    *ppPalette = &palette->base;
    return S_OK;
}
void WINAPI D3DPalette_Lock(struct D3DPalette *palette, unsigned long **data, unsigned long flags) {
    (void)flags;
    if (data) *data = palette ? (unsigned long *)palette->Data : NULL;
}
void WINAPI D3DDevice_SetPalette(unsigned long stage, struct D3DPalette *palette) {
    if (stage >= D3DTSS_MAXSTAGES) return;
    if (g_palettes[stage] != palette && g_textures[stage] && g_textures[stage]->format == D3DFMT_P8)
        g_textures[stage]->uploaded = 0;
    g_palettes[stage] = palette;
}

/* ---------- D3DX Math */

D3DXMATRIX * WINAPI D3DXMatrixOrthoLH(D3DXMATRIX *out, float width, float height, float near_plane, float far_plane) {
    if (!out) return NULL;
    memset(out, 0, sizeof(*out));
    out->_11 = 2.0f / width;
    out->_22 = 2.0f / height;
    out->_33 = 1.0f / (far_plane - near_plane);
    out->_43 = -near_plane / (far_plane - near_plane);
    out->_44 = 1.0f;
    return out;
}

D3DXMATRIX * WINAPI D3DXMatrixPerspectiveLH(D3DXMATRIX *out, float width, float height, float near_plane, float far_plane) {
    if (!out) return NULL;
    memset(out, 0, sizeof(*out));
    out->_11 = 2.0f * near_plane / width;
    out->_22 = 2.0f * near_plane / height;
    out->_33 = far_plane / (far_plane - near_plane);
    out->_34 = 1.0f;
    out->_43 = -near_plane * far_plane / (far_plane - near_plane);
    return out;
}

struct D3DXVECTOR4 * WINAPI D3DXVec4Transform(struct D3DXVECTOR4 *out, const struct D3DXVECTOR4 *v, const D3DXMATRIX *m) {
    if (!out || !v || !m) return out;
    float x = v->x, y = v->y, z = v->z, w = v->w;
    out->x = x * m->_11 + y * m->_21 + z * m->_31 + w * m->_41;
    out->y = x * m->_12 + y * m->_22 + z * m->_32 + w * m->_42;
    out->z = x * m->_13 + y * m->_23 + z * m->_33 + w * m->_43;
    out->w = x * m->_14 + y * m->_24 + z * m->_34 + w * m->_44;
    return out;
}

const char * WINAPI D3DXGetErrorStringA(HRESULT hr) {
    (void)hr;
    return "D3D_OK";
}
