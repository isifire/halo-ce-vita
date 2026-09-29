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

/* ---------- State Tables & Global Storage */

DWORD D3D__RenderState[D3DRS_MAX];
DWORD D3D__TextureState[D3DTSS_MAXSTAGES][32];
WORD *D3D__IndexData = NULL;

static volatile unsigned int vita_flip_count = 0;
volatile unsigned int *d3d_find_flipcount(void) {
    return &vita_flip_count;
}

/* ---------- Device & Surface State */

static int g_dummy_device_data = 1;
static D3DDevice *g_d3d_device = (D3DDevice *)&g_dummy_device_data;
static D3DSurface g_back_buffer;
static D3DSurface g_depth_buffer;
static int g_vitagl_initialized = 0;
static D3DVIEWPORT8 g_viewport = { 0, 0, 960, 544, 0.0f, 1.0f };

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
        vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
        g_vitagl_initialized = 1;

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
    vglSwapBuffers(GL_FALSE);
    vita_flip_count++;
}

void WINAPI D3DDevice_Clear(unsigned long count, const struct _D3DRECT *rects, unsigned long flags, unsigned long color, float z, unsigned long stencil) {
    (void)count; (void)rects;
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        float a = ((color >> 24) & 0xff) * (1.0f / 255.0f);
        float r = ((color >> 16) & 0xff) * (1.0f / 255.0f);
        float g = ((color >> 8) & 0xff) * (1.0f / 255.0f);
        float b = (color & 0xff) * (1.0f / 255.0f);
        glClearColor(r, g, b, a);
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_ZBUFFER) {
        glClearDepthf(z);
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & D3DCLEAR_STENCIL) {
        glClearStencil((GLint)stencil);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (mask) glClear(mask);
}

void WINAPI D3DDevice_Begin(enum _D3DPRIMITIVETYPE type) { (void)type; }
void WINAPI D3DDevice_End(void) { }
unsigned long WINAPI D3DDevice_Release(void) { return 0; }
long WINAPI D3DDevice_PersistDisplay(void) { return S_OK; }
void WINAPI Direct3D_SetPushBufferSize(unsigned long psize, unsigned long usize) { (void)psize; (void)usize; }

/* ---------- Viewport & Targets */

void WINAPI D3DDevice_SetViewport(const struct _D3DVIEWPORT8 *viewport) {
    if (!viewport) return;
    g_viewport = *viewport;
    glViewport(viewport->X, 544 - (viewport->Y + viewport->Height), viewport->Width, viewport->Height);
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
    (void)render_target; (void)new_z_stencil;
}

void WINAPI D3DSurface_GetDesc(struct D3DSurface *surface, struct _D3DSURFACE_DESC *desc) {
    if (!desc) return;
    memset(desc, 0, sizeof(*desc));
    desc->Width = 960;
    desc->Height = 544;
    desc->Format = D3DFMT_LIN_A8R8G8B8;
    desc->Type = D3DRTYPE_SURFACE;
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

static struct d3d_vertex_buffer_impl *g_streams[16] = {0};
static unsigned int g_stream_strides[16] = {0};
static struct d3d_index_buffer_impl *g_current_ib = NULL;

long WINAPI D3DDevice_CreateVertexBuffer(unsigned int length, unsigned long usage, unsigned long fvf, unsigned long pool, struct D3DVertexBuffer **ppVertexBuffer) {
    (void)pool;
    struct d3d_vertex_buffer_impl *vb = (struct d3d_vertex_buffer_impl *)calloc(1, sizeof(*vb));
    if (!vb) return E_OUTOFMEMORY;
    vb->base.Common = D3DCOMMON_TYPE_VERTEXBUFFER | D3DCOMMON_D3DCREATED | 1;
    vb->size = length;
    vb->usage = usage;
    vb->fvf = fvf;
    vb->cpu_data = halo_vita_alloc(length, 64, 0, PAGE_READWRITE);
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
    ib->cpu_data = halo_vita_alloc(length, 64, 0, PAGE_READWRITE);
    if (!ib->cpu_data) ib->cpu_data = malloc(length);
    ib->base.Data = (DWORD)ib->cpu_data;
    if (ppIndexBuffer) *ppIndexBuffer = (struct D3DIndexBuffer *)ib;
    return S_OK;
}

void WINAPI D3DDevice_SetStreamSource(unsigned int stream_number, struct D3DVertexBuffer *stream_data, unsigned int stride) {
    if (stream_number < 16) {
        g_streams[stream_number] = (struct d3d_vertex_buffer_impl *)stream_data;
        g_stream_strides[stream_number] = stride;
    }
}

void WINAPI D3DDevice_SetIndices(struct D3DIndexBuffer *index_data, unsigned int base_vertex_index) {
    (void)base_vertex_index;
    g_current_ib = (struct d3d_index_buffer_impl *)index_data;
    D3D__IndexData = index_data ? (WORD *)index_data->Data : NULL;
}

/* ---------- Textures */

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
};

static struct d3d_texture_impl *g_textures[D3DTSS_MAXSTAGES] = {0};

long WINAPI D3DDevice_CreateTexture(unsigned int width, unsigned int height, unsigned int levels, unsigned long usage, enum _D3DFORMAT format, unsigned long pool, struct D3DTexture **ppTexture) {
    (void)usage; (void)pool;
    struct d3d_texture_impl *tex = (struct d3d_texture_impl *)calloc(1, sizeof(*tex));
    if (!tex) return E_OUTOFMEMORY;
    tex->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    tex->width = width;
    tex->height = height;
    tex->levels = levels ? levels : 1;
    tex->format = format;

    unsigned int bpp = 4;
    if (format == D3DFMT_DXT1) bpp = 1;
    else if (format == D3DFMT_DXT3 || format == D3DFMT_DXT5) bpp = 1;
    else if (format == D3DFMT_R5G6B5 || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4) bpp = 2;

    tex->pitch = width * bpp;
    tex->data_size = (tex->pitch * height * 3) / 2;
    if (tex->data_size < 128) tex->data_size = 128;
    tex->cpu_data = halo_vita_alloc(tex->data_size, 128, 0, PAGE_READWRITE);
    if (!tex->cpu_data) tex->cpu_data = malloc(tex->data_size);
    tex->base.Data = (DWORD)tex->cpu_data;

    tex->base.Format = ((DWORD)format << D3DFORMAT_FORMAT_SHIFT) | (2 << D3DFORMAT_DIMENSION_SHIFT);
    tex->base.Size = ((width - 1)) | ((height - 1) << 12);

    if (ppTexture) *ppTexture = (struct D3DTexture *)tex;
    return S_OK;
}

void WINAPI D3DTexture_LockRect(struct D3DTexture *texture, unsigned int level, struct _D3DLOCKED_RECT *locked_rect, const struct tagRECT *rect, unsigned long flags) {
    (void)level; (void)rect; (void)flags;
    if (locked_rect && texture) {
        locked_rect->pBits = (void *)texture->Data;
        struct d3d_texture_impl *impl = (struct d3d_texture_impl *)texture;
        locked_rect->Pitch = impl->pitch ? impl->pitch : (impl->width * 4);
    }
}

void WINAPI D3DTexture_GetLevelDesc(struct D3DTexture *texture, unsigned int level, struct _D3DSURFACE_DESC *desc) {
    (void)level;
    if (desc && texture) {
        struct d3d_texture_impl *impl = (struct d3d_texture_impl *)texture;
        desc->Width = impl->width;
        desc->Height = impl->height;
        desc->Format = impl->format;
        desc->Type = D3DRTYPE_TEXTURE;
    }
}

long WINAPI D3DTexture_GetSurfaceLevel(struct D3DTexture *texture, unsigned int level, struct D3DSurface **surface) {
    (void)level;
    if (surface && texture) {
        *surface = (struct D3DSurface *)texture;
    }
    return S_OK;
}

void WINAPI D3DDevice_SetTexture(unsigned long stage, struct D3DBaseTexture *texture) {
    if (stage < D3DTSS_MAXSTAGES) {
        g_textures[stage] = (struct d3d_texture_impl *)texture;
        glActiveTexture(GL_TEXTURE0 + stage);
        if (texture) {
            struct d3d_texture_impl *impl = (struct d3d_texture_impl *)texture;
            if (!impl->gl_id) {
                glGenTextures(1, &impl->gl_id);
                glBindTexture(GL_TEXTURE_2D, impl->gl_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

                if (impl->format == D3DFMT_DXT1) {
                    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, impl->width, impl->height, 0, impl->data_size, impl->cpu_data);
                } else if (impl->format == D3DFMT_DXT3) {
                    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, impl->width, impl->height, 0, impl->data_size, impl->cpu_data);
                } else if (impl->format == D3DFMT_DXT5) {
                    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, impl->width, impl->height, 0, impl->data_size, impl->cpu_data);
                } else {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, impl->width, impl->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, impl->cpu_data);
                }
                impl->uploaded = 1;
            } else {
                glBindTexture(GL_TEXTURE_2D, impl->gl_id);
            }
            glEnable(GL_TEXTURE_2D);
        } else {
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
        }
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
    if (!g_streams[0] || !g_streams[0]->cpu_data) return;
    unsigned int stride = g_stream_strides[0] ? g_stream_strides[0] : 32;
    const unsigned char *base = (const unsigned char *)g_streams[0]->cpu_data;

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
    glDrawArrays(d3d_to_gl_prim(primitive_type), (GLint)start_vertex, (GLsizei)vertex_count);
}

void WINAPI D3DDevice_DrawIndexedVertices(enum _D3DPRIMITIVETYPE primitive_type, unsigned int vertex_count, const unsigned short *index_data) {
    if (!g_streams[0] || !g_streams[0]->cpu_data) return;
    unsigned int stride = g_stream_strides[0] ? g_stream_strides[0] : 32;
    const unsigned char *base = (const unsigned char *)g_streams[0]->cpu_data;

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
        glDrawElements(d3d_to_gl_prim(primitive_type), (GLsizei)vertex_count, GL_UNSIGNED_SHORT, indices);
    }
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
        if (fields[1]) halo_vita_free((void *)fields[1]);
        free(resource);
    }
    return count;
}

int WINAPI D3DResource_IsBusy(struct D3DResource *resource) { (void)resource; return 0; }
void WINAPI D3DResource_BlockUntilNotBusy(struct D3DResource *resource) { (void)resource; }

/* ---------- Render States */

void WINAPI D3DDevice_SetRenderStateNotInline(enum _D3DRENDERSTATETYPE state, unsigned long value) {
    if ((unsigned long)state < D3DRS_MAX) {
        D3D__RenderState[state] = value;
    }
    if (state == D3DRS_ZENABLE) {
        if (value) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    } else if (state == D3DRS_ZWRITEENABLE) {
        glDepthMask(value ? GL_TRUE : GL_FALSE);
    } else if (state == D3DRS_ALPHABLENDENABLE) {
        if (value) glEnable(GL_BLEND); else glDisable(GL_BLEND);
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
DEFINE_RENDER_STATE_STUB(PSTextureModes, D3DRS_PSTEXTUREMODES)
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
void WINAPI D3DDevice_SetTextureState_BumpEnv(unsigned long stage, enum _D3DTEXTURESTAGESTATETYPE type, unsigned long value) { (void)stage; (void)type; (void)value; }
void WINAPI D3DDevice_SetTextureState_ColorKeyColor(unsigned long stage, unsigned long color) { (void)stage; (void)color; }
void WINAPI D3DDevice_SetTextureState_TexCoordIndex(unsigned long stage, unsigned long index) { (void)stage; (void)index; }

/* ---------- Immediate Vertex Data */

void WINAPI D3DDevice_SetVertexData2f(int register_index, float x, float y) { (void)register_index; (void)x; (void)y; }
void WINAPI D3DDevice_SetVertexData2s(int register_index, short x, short y) { (void)register_index; (void)x; (void)y; }
void WINAPI D3DDevice_SetVertexData4f(int register_index, float x, float y, float z, float w) { (void)register_index; (void)x; (void)y; (void)z; (void)w; }
void WINAPI D3DDevice_SetVertexData4ub(int register_index, unsigned char b0, unsigned char b1, unsigned char b2, unsigned char b3) { (void)register_index; (void)b0; (void)b1; (void)b2; (void)b3; }
void WINAPI D3DDevice_SetVertexDataColor(int register_index, unsigned long color) { (void)register_index; (void)color; }

/* ---------- Shaders & Capabilities */

long WINAPI D3DDevice_CreateVertexShader(const unsigned long *declaration, const unsigned long *function, unsigned long *shader_handle, unsigned long usage) {
    (void)declaration; (void)function; (void)usage;
    static unsigned long next_shader = 1;
    if (shader_handle) *shader_handle = next_shader++;
    return S_OK;
}
void WINAPI D3DDevice_DeleteVertexShader(unsigned long shader_handle) { (void)shader_handle; }
void WINAPI D3DDevice_LoadVertexShader(unsigned long shader_handle, unsigned long address) { (void)shader_handle; (void)address; }
void WINAPI D3DDevice_SelectVertexShader(unsigned long shader_handle, unsigned long address) { (void)shader_handle; (void)address; }
void WINAPI D3DDevice_GetVertexShaderSize(unsigned long shader_handle, unsigned int *size) { (void)shader_handle; if (size) *size = 0; }
void WINAPI D3DDevice_SetVertexShader(unsigned long shader_handle) { (void)shader_handle; }
void WINAPI D3DDevice_SetVertexShaderConstant(int register_index, const void *constant_data, unsigned long constant_count) {
    (void)register_index; (void)constant_data; (void)constant_count;
}
void WINAPI D3DDevice_SetShaderConstantMode(unsigned long mode) { (void)mode; }
void WINAPI D3DDevice_SetPixelShaderProgram(struct _D3DPixelShaderDef *program) { (void)program; }
void WINAPI D3DDevice_SetFlickerFilter(unsigned long filter) { (void)filter; }
void WINAPI D3DDevice_SetSoftDisplayFilter(int enable) { (void)enable; }
void WINAPI D3DDevice_SetVerticalBlankCallback(void (*callback)(unsigned long)) { (void)callback; }
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
long WINAPI D3DDevice_CreateCubeTexture(unsigned int edge, unsigned int levels, unsigned long usage, enum _D3DFORMAT format, unsigned long pool, struct D3DCubeTexture **ppCube) {
    struct d3d_cube_texture_impl {
        struct D3DCubeTexture base;
        unsigned int edge;
        unsigned int face_size;
        unsigned char pixels[1];
    };
    struct d3d_cube_texture_impl *cube;
    unsigned int bpp = 4;
    unsigned int face_size;
    (void)levels; (void)usage; (void)pool;
    if (!ppCube || !edge) return E_INVALIDARG;
    if (format == D3DFMT_R5G6B5 || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4)
        bpp = 2;
    face_size = edge * edge * bpp;
    cube = (struct d3d_cube_texture_impl *)calloc(1, sizeof(*cube) - 1 + face_size * 6);
    if (!cube) return E_OUTOFMEMORY;
    cube->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    cube->base.Data = (DWORD)cube->pixels;
    cube->base.Format = ((DWORD)format << D3DFORMAT_FORMAT_SHIFT) | (3 << D3DFORMAT_DIMENSION_SHIFT);
    cube->base.Size = ((edge - 1)) | ((edge - 1) << 12);
    cube->edge = edge;
    cube->face_size = face_size;
    *ppCube = &cube->base;
    return S_OK;
}
void WINAPI D3DCubeTexture_LockRect(struct D3DCubeTexture *cube, enum _D3DCUBEMAP_FACES face, unsigned int level, struct _D3DLOCKED_RECT *rect, const struct tagRECT *r, unsigned long flags) {
    struct d3d_cube_texture_impl {
        struct D3DCubeTexture base;
        unsigned int edge;
        unsigned int face_size;
        unsigned char pixels[1];
    };
    struct d3d_cube_texture_impl *impl = (struct d3d_cube_texture_impl *)cube;
    (void)level; (void)r; (void)flags;
    if (rect) {
        rect->Pitch = impl ? (int)(impl->face_size / impl->edge) : 0;
        rect->pBits = impl && (unsigned int)face < 6 ? impl->pixels + (unsigned int)face * impl->face_size : NULL;
    }
}
long WINAPI D3DDevice_CreateVolumeTexture(unsigned int w, unsigned int h, unsigned int d, unsigned int l, unsigned long u, enum _D3DFORMAT f, unsigned long p, struct D3DVolumeTexture **ppVol) {
    struct d3d_volume_texture_impl {
        struct D3DVolumeTexture base;
        unsigned int width, height, depth, bpp;
        unsigned char pixels[1];
    };
    struct d3d_volume_texture_impl *volume;
    unsigned int bpp = 4;
    unsigned int data_size;
    (void)l; (void)u; (void)p;
    if (!ppVol || !w || !h || !d) return E_INVALIDARG;
    if (f == D3DFMT_R5G6B5 || f == D3DFMT_A1R5G5B5 || f == D3DFMT_A4R4G4B4)
        bpp = 2;
    data_size = w * h * d * bpp;
    volume = (struct d3d_volume_texture_impl *)calloc(1, sizeof(*volume) - 1 + data_size);
    if (!volume) return E_OUTOFMEMORY;
    volume->base.Common = D3DCOMMON_TYPE_TEXTURE | D3DCOMMON_D3DCREATED | 1;
    volume->base.Data = (DWORD)volume->pixels;
    volume->base.Format = ((DWORD)f << D3DFORMAT_FORMAT_SHIFT) | (3 << D3DFORMAT_DIMENSION_SHIFT);
    volume->base.Size = ((w - 1)) | ((h - 1) << 12);
    volume->width = w; volume->height = h; volume->depth = d; volume->bpp = bpp;
    *ppVol = &volume->base;
    return S_OK;
}
void WINAPI D3DVolumeTexture_LockBox(struct D3DVolumeTexture *vol, unsigned int level, struct _D3DLOCKED_BOX *box, const struct _D3DBOX *b, unsigned long flags) {
    struct d3d_volume_texture_impl {
        struct D3DVolumeTexture base;
        unsigned int width, height, depth, bpp;
        unsigned char pixels[1];
    };
    struct d3d_volume_texture_impl *impl = (struct d3d_volume_texture_impl *)vol;
    (void)level; (void)b; (void)flags;
    if (box) {
        box->RowPitch = impl ? (int)(impl->width * impl->bpp) : 0;
        box->SlicePitch = impl ? (int)(impl->width * impl->height * impl->bpp) : 0;
        box->pBits = impl ? impl->pixels : NULL;
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
void WINAPI D3DDevice_SetPalette(unsigned long stage, struct D3DPalette *palette) { (void)stage; (void)palette; }

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
