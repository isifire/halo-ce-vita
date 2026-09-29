/*
SHELL.C
*/

/* ---------- headers */

#include "cseries.h"
#include "shell.h"
#include "errors.h"
#include "tag_files.h"
#include "real_math.h"
#include "game_state.h"
#include "rasterizer.h"
#include "integer_math.h"
#include "input.h"
#include "sound_manager.h"

/* ---------- globals */

static boolean application_paused;

#ifdef HALO_VITA
static void shell_vita_boot_log(const char *message)
{
	FILE *file = fopen("ux0:data/halo/boot.log", "a");
	if (file)
	{
		fprintf(file, "shell_initialize: %s\n", message);
		fclose(file);
	}
}
#else
#define shell_vita_boot_log(message) ((void)0)
#endif

/* ---------- public code */

boolean shell_initialize(
	void)
{
	boolean success = FALSE;
	boolean platform_initialized = FALSE;
	
	shell_vita_boot_log("cseries_initialize begin");
	cseries_initialize();
	shell_vita_boot_log("cseries_initialize ok");
	platform_initialized = shell_platform_initialize();
	shell_vita_boot_log(platform_initialized ? "shell_platform_initialize ok" : "shell_platform_initialize failed");
	
	if (platform_initialized)
	{
		boolean rasterizer_initialized;
		
		shell_vita_boot_log("errors_initialize begin");
		errors_initialize();
		shell_vita_boot_log("errors_initialize ok");
		tag_files_open();
		shell_vita_boot_log("tag_files_open ok");
		real_math_initialize();
		shell_vita_boot_log("real_math_initialize ok");
		game_state_initialize();
		shell_vita_boot_log("game_state_initialize ok");
		
		shell_vita_boot_log("rasterizer_initialize begin");
		rasterizer_initialized = rasterizer_initialize();
		shell_vita_boot_log(rasterizer_initialized ? "rasterizer_initialize ok" : "rasterizer_initialize failed");
		
		if (rasterizer_initialized)
		{
			input_initialize();
			shell_vita_boot_log("input_initialize ok");
			sound_initialize();
			shell_vita_boot_log("sound_initialize ok");
			
			success = TRUE;
		}
			
		shell_vita_boot_log("shell_platform_verify begin");
		shell_platform_verify();
		shell_vita_boot_log("shell_platform_verify ok");
	}
	
	return success;
}

void shell_dispose(
	void)
{
	sound_dispose();
	input_dispose();
	rasterizer_dispose();
	real_math_dispose();
	tag_files_close();
	errors_dispose();
	shell_platform_dispose();
	cseries_dispose();

	return;
}

boolean shell_application_is_paused(
	void)
{
	return application_paused;
}

void shell_application_pause(
	boolean paused)
{
	if (application_paused!=paused)
	{
		application_paused = paused;
		shell_screen_pause(paused);
	}

	return;
}

boolean shell_running_import_tool(void)
{
	return FALSE;
}
