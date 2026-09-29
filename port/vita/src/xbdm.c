/* Xbox Debug Monitor (xbdm.dll) stubs for PlayStation Vita. */
#include <xtl.h>

#ifndef XBDM_ENDOFLIST
#define XBDM_ENDOFLIST ((HRESULT)0x82DB0104L)
#endif
#ifndef XBDM_NOERR
#define XBDM_NOERR 0
#endif

typedef void *PDM_WALK_MODULES;
typedef void *PDMN_MODLOAD;
typedef void *PDM_WALK_MODSECT;
typedef void *PDMN_SECTIONLOAD;

HRESULT __stdcall DmWalkLoadedModules(PDM_WALK_MODULES *walk, PDMN_MODLOAD module)
{
	(void)walk;
	(void)module;
	return (HRESULT)XBDM_ENDOFLIST;
}

HRESULT __stdcall DmWalkModuleSections(PDM_WALK_MODSECT *walk, LPCSTR module_name, PDMN_SECTIONLOAD section)
{
	(void)walk;
	(void)module_name;
	(void)section;
	return (HRESULT)XBDM_ENDOFLIST;
}

HRESULT __stdcall DmCloseModuleSections(PDM_WALK_MODSECT walk)
{
	(void)walk;
	return (HRESULT)XBDM_NOERR;
}
