/*
 * Isolated VitaGL shader-link probe for Halo CE Vita.
 *
 * This is packaged as a separate title so a VitaGL/Cg abort cannot take down
 * the working game build. It tests a minimal one-sampler program first, then
 * a scoped four-sampler combiner-like fragment shader. Checkpoints are flushed
 * to ux0:data/halo/shader-probe.log before each deferred compile/link stage.
 */
#include <psp2/kernel/threadmgr.h>
#include <vitaGL.h>
#include <stdio.h>
#include <string.h>

static void probe_log(const char *message)
{
	FILE *log = fopen("ux0:data/halo/shader-probe.log", "a");
	if (!log)
		log = fopen("ux0:data/shader-probe.log", "a");
	if (log)
	{
		fprintf(log, "%s\n", message ? message : "<null>");
		fclose(log);
	}
}

/* VitaGL calls this hook from the deferred translator and Cg compiler. */
void halo_vita_glsl_checkpoint(const char *message)
{
	probe_log(message);
}

static const char vertex_source[] =
	"#version 100\n"
	"attribute vec3 a_position; attribute vec2 a_uv; attribute vec4 a_color;\n"
	"varying vec2 v_uv; varying vec4 v_color;\n"
	"varying vec4 xD0; varying vec4 xD1;\n"
	"varying vec4 xT0; varying vec4 xT1; varying vec4 xT2; varying vec4 xT3;\n"
	"void main(){ gl_Position=vec4(a_position,1.0); v_uv=a_uv; v_color=a_color;\n"
	"xD0=a_color; xD1=a_color; xT0=vec4(a_uv,0.0,1.0); xT1=xT0; xT2=xT0; xT3=xT0; }\n";

static const char single_sampler_fragment[] =
	"#version 100\nprecision mediump float;\n"
	"uniform sampler2D tex0; varying vec2 v_uv; varying vec4 v_color;\n"
	"void main(){ vec4 c=texture2D(tex0,v_uv)*v_color;"
	"gl_FragColor=vec4(0.05,0.65,0.15,1.0)+c*0.01; }\n";

static const char four_sampler_fragment[] =
	"#version 100\nprecision highp float;\n"
	"uniform sampler2D tex0; uniform sampler2D tex1;\n"
	"uniform sampler2D tex2; uniform sampler2D tex3;\n"
	"varying vec4 xD0; varying vec4 xD1;\n"
	"varying vec4 xT0; varying vec4 xT1; varying vec4 xT2; varying vec4 xT3;\n"
	"void main(){\n"
	"vec4 t0=texture2D(tex0,xT0.xy); vec4 t1=texture2D(tex1,xT1.xy);\n"
	"vec4 t2=texture2D(tex2,xT2.xy); vec4 t3=texture2D(tex3,xT3.xy);\n"
	"vec4 r0=vec4(0.0,0.0,0.0,t0.a);\n"
	"{ vec3 cA=max(t0.rgb,0.0); vec3 cB=max(xD0.rgb,0.0);"
	"vec3 cAB=cA*cB; r0.rgb=clamp(cAB,-1.0,1.0); }\n"
	"{ vec3 cA=max(t1.rgb,0.0); vec3 cB=max(r0.rgb,0.0);"
	"vec3 cC=max(xD1.rgb,0.0); vec3 cD=max(t2.rgb,0.0);"
	"r0.rgb=clamp(cA*cB+cC*cD,-1.0,1.0); }\n"
	"{ vec3 cA=max(t3.rgb,0.0); vec3 cB=max(r0.rgb,0.0);"
	"r0.rgb=clamp(cA*cB,-1.0,1.0); }\n"
	"gl_FragColor=vec4(0.05,0.65,0.15,1.0)+clamp(r0,0.0,1.0)*0.01; }\n";

static GLuint link_test_program(const char *label, const char *fragment_source_text)
{
	GLuint vertex, fragment, program;
	GLint linked = GL_FALSE;
	char message[128];
	char info[512] = {0};

	vertex = glCreateShader(GL_VERTEX_SHADER);
	fragment = glCreateShader(GL_FRAGMENT_SHADER);
	if (!vertex || !fragment)
	{
		snprintf(message, sizeof(message), "probe: %s create shader failed v=%u f=%u",
			label, (unsigned int)vertex, (unsigned int)fragment);
		probe_log(message);
		return 0;
	}
	glShaderSource(vertex, 1, (const GLchar * const *)&vertex_source, NULL);
	glShaderSource(fragment, 1, (const GLchar * const *)&fragment_source_text, NULL);
	probe_log("probe: compile vertex requested (deferred by VitaGL)");
	glCompileShader(vertex);
	probe_log("probe: compile fragment requested (deferred by VitaGL)");
	glCompileShader(fragment);
	program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glBindAttribLocation(program, 0, "a_position");
	glBindAttribLocation(program, 1, "a_uv");
	glBindAttribLocation(program, 2, "a_color");
	snprintf(message, sizeof(message), "probe: %s glLinkProgram begin", label);
	probe_log(message);
	glLinkProgram(program);
	snprintf(message, sizeof(message), "probe: %s glLinkProgram returned", label);
	probe_log(message);
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	glGetProgramInfoLog(program, sizeof(info), NULL, info);
	snprintf(message, sizeof(message), "probe: %s linked=%d log=%s", label,
		(int)linked, info[0] ? info : "<empty>");
	probe_log(message);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	if (!linked)
	{
		glDeleteProgram(program);
		return 0;
	}
	return program;
}

static void draw_triangle(GLuint program)
{
	static const GLfloat positions[] = { -0.72f,-0.72f,0.0f, 0.72f,-0.72f,0.0f, 0.0f,0.72f,0.0f };
	static const GLfloat uvs[] = { 0,0, 1,0, 0.5f,1 };
	static const GLfloat colors[] = { 1,1,1,1, 1,1,1,1, 1,1,1,1 };
	glUseProgram(program);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, uvs);
	glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 0, colors);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glDisableVertexAttribArray(2);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(0);
	glUseProgram(0);
}

int main(void)
{
	GLuint simple_program, four_program;
	GLint units = 0;
	GLboolean resolution_fallback;
	GLenum init_error;

	probe_log("probe: app entry");
	/* VitaGL returns whether it had to clamp the requested resolution, not
	 * whether initialization succeeded. GL_FALSE is the normal 960x544 case. */
	probe_log("probe: vglInitExtended begin");
	resolution_fallback = vglInitExtended(0, 960, 544, 16 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
	init_error = glGetError();
	{
		char message[128];
		snprintf(message, sizeof(message),
			"probe: vglInitExtended returned resolution_fallback=%d gl_error=0x%x; continuing",
			(int)resolution_fallback, (unsigned int)init_error);
		probe_log(message);
	}
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
	{
		char message[128];
		snprintf(message, sizeof(message), "probe: VitaGL ready fragment_units=%d", (int)units);
		probe_log(message);
	}
	probe_log("probe: beginning one-sampler baseline");
	simple_program = link_test_program("one-sampler", single_sampler_fragment);
	if (!simple_program)
	{
		probe_log("probe: one-sampler failed; not attempting complex shader");
		for (;;)
		{
			glClearColor(0.6f, 0.02f, 0.02f, 1.0f);
			glClear(GL_COLOR_BUFFER_BIT);
			vglSwapBuffers(GL_FALSE);
			sceKernelDelayThread(100000);
		}
	}
	probe_log("probe: beginning scoped four-sampler combiner test");
	four_program = link_test_program("scoped-four-sampler", four_sampler_fragment);
	if (four_program)
		probe_log("probe: both shader links passed");
	else
		probe_log("probe: complex shader returned an unsuccessful link");
	for (;;)
	{
		glClearColor(0.02f, 0.03f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		draw_triangle(four_program ? four_program : simple_program);
		vglSwapBuffers(GL_FALSE);
		sceKernelDelayThread(16667);
	}
	return 0;
}
