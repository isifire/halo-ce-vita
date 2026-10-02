#ifndef VITA_HUD_TRACE_H
#define VITA_HUD_TRACE_H

#ifdef HALO_VITA
#include <stdio.h>
static inline void vita_hud_trace(const char *stage)
{
	static int s_hud_trace_count = 0;
	if (s_hud_trace_count >= 15) return;
	s_hud_trace_count++;
	FILE *log = fopen("ux0:data/halo/boot.log", "a");
	if (log)
	{
		fprintf(log, "interface: %s\n", stage);
		fclose(log);
	}
}
#else
#define vita_hud_trace(stage) ((void)0)
#endif

#endif
