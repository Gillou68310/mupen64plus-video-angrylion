#include "z64.h"
#include <SDL.h>
#include <SDL_syswm.h>

#define M64P_PLUGIN_PROTOTYPES 1
#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_plugin.h"
#include "m64p_types.h"

#define PLUGIN_VERSION              0x020000
#define VIDEO_PLUGIN_API_VERSION	  0x020200
#define CONFIG_API_VERSION          0x020000
#define VIDEXT_API_VERSION          0x030000

extern int SaveLoaded;
extern UINT32 command_counter;
extern INLINE void popmessage(const char* err, ...);
extern INLINE void fatalerror(const char* err, ...);
int rdp_init();
int rdp_close();
int rdp_update();
void rdp_process_list(void);

LPDIRECTDRAW7 lpdd = 0;
LPDIRECTDRAWSURFACE7 lpddsprimary; 
LPDIRECTDRAWSURFACE7 lpddsback;
DDSURFACEDESC2 ddsd;
HRESULT res;
RECT dst, src;
INT32 pitchindwords;
SDL_SysWMinfo sysInfo;
FILE* zeldainfo = 0;
GFX_INFO gfx;
int ProcessDListShown = 0;
const int screen_width = 640, screen_height = 480;

EXPORT BOOL CALL InitiateGFX (GFX_INFO Gfx_Info)
{
  gfx = Gfx_Info;
  return TRUE;
}

EXPORT void CALL MoveScreen (int xpos, int ypos)
{
	POINT p;
	p.x = p.y = 0;
	GetClientRect(sysInfo.window, &dst);
	ClientToScreen(sysInfo.window, &p);
	OffsetRect(&dst, p.x, p.y);
}

EXPORT void CALL ProcessDList(void)
{
	if (!ProcessDListShown)
	{
		popmessage("ProcessDList");
		ProcessDListShown = 1;
	}
}
 
EXPORT void CALL ProcessRDPList(void)
{
	rdp_process_list();
	return;
}

EXPORT void CALL RomClosed (void)
{
	rdp_close();
	if (lpddsback)
	{
		IDirectDrawSurface_Release(lpddsback);
		lpddsback = 0;
	}
	if (lpddsprimary)
	{
		IDirectDrawSurface_Release(lpddsprimary);
		lpddsprimary = 0;
	}
	if (lpdd)
	{
		IDirectDraw_Release(lpdd);
		lpdd = 0;
	}

	SaveLoaded = 1;
	command_counter = 0;
}

EXPORT void CALL ShowCFB (void)
{
	rdp_update();
}

EXPORT void CALL UpdateScreen (void)
{
	rdp_update();
}

EXPORT void CALL ViStatusChanged (void)
{
}
 
EXPORT void CALL ViWidthChanged (void)
{
}

EXPORT void CALL ChangeWindow (void)
{
}

EXPORT int CALL RomOpen(void)
{
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  SDL_SetVideoMode(screen_width, screen_height, 32, SDL_HWSURFACE);
  SDL_WM_SetCaption("Angrylion", "M64+ Video");

  SDL_VERSION(&sysInfo.version);
  if(SDL_GetWMInfo(&sysInfo) <= 0)
    printf("Unable to get window handle");

  RECT bigrect, smallrect;
	
	GetWindowRect(sysInfo.window,&bigrect);
	GetClientRect(sysInfo.window,&smallrect);
	int rightdiff = screen_width - smallrect.right;
	int bottomdiff = screen_height - smallrect.bottom;
	MoveWindow(sysInfo.window, bigrect.left, bigrect.top, bigrect.right - bigrect.left + rightdiff, bigrect.bottom - bigrect.top + bottomdiff, TRUE);

	DDPIXELFORMAT ftpixel;
	LPDIRECTDRAWCLIPPER lpddcl;

	res = DirectDrawCreateEx(0, (LPVOID*)&lpdd, IID_IDirectDraw7, 0);
	if(res != DD_OK) 
		fatalerror("Couldn't create a DirectDraw object");
	res = IDirectDraw_SetCooperativeLevel(lpdd, sysInfo.window, DDSCL_NORMAL);
	if(res != DD_OK) 
		fatalerror("Couldn't set a cooperative level. Error code %x", res);

	memset(&ddsd, 0, sizeof(ddsd));
	ddsd.dwSize = sizeof(ddsd);
	ddsd.dwFlags = DDSD_CAPS;
	ddsd.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
	
	res = IDirectDraw_CreateSurface(lpdd, &ddsd, &lpddsprimary, 0);
	if(res != DD_OK)
		fatalerror("CreateSurface for a primary surface failed. Error code %x", res);

	ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
	ddsd.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN;
	ddsd.dwWidth = PRESCALE_WIDTH;
	ddsd.dwHeight = PRESCALE_HEIGHT;
	memset(&ftpixel, 0, sizeof(ftpixel));
	ftpixel.dwSize = sizeof(ftpixel);
	ftpixel.dwFlags = DDPF_RGB;
	ftpixel.dwRGBBitCount = 32;
	ftpixel.dwRBitMask = 0xff0000;
	ftpixel.dwGBitMask = 0xff00;
	ftpixel.dwBBitMask = 0xff;
	ddsd.ddpfPixelFormat = ftpixel;
	res = IDirectDraw_CreateSurface(lpdd, &ddsd, &lpddsback, 0);
	if (res == DDERR_INVALIDPIXELFORMAT)
		fatalerror("ARGB8888 is not supported. You can try changing desktop color depth to 32-bit, but most likely that won't help.");
	else if(res != DD_OK)
		fatalerror("CreateSurface for a secondary surface failed. Error code %x", res);

	res = IDirectDrawSurface_GetSurfaceDesc(lpddsback, &ddsd);
	if (res != DD_OK)
		fatalerror("GetSurfaceDesc failed.");
	if ((ddsd.lPitch & 3) || ddsd.lPitch < (PRESCALE_WIDTH << 2))
		fatalerror("Pitch of a secondary surface is either not 32 bit aligned or two small.");
	pitchindwords = ddsd.lPitch >> 2;
	
	res = IDirectDraw_CreateClipper(lpdd, 0, &lpddcl, 0);
	if (res != DD_OK)
		fatalerror("Couldn't create a clipper.");
	res = IDirectDrawClipper_SetHWnd(lpddcl, 0, sysInfo.window);
	if (res != DD_OK)
		fatalerror("Couldn't register a windows handle as a clipper.");
	res = IDirectDrawSurface_SetClipper(lpddsprimary, lpddcl);
	if (res != DD_OK)
		fatalerror("Couldn't attach a clipper to a surface.");
	
	src.top = src.left = 0; 
	src.bottom = 0;
	src.right = PRESCALE_WIDTH;
	
	POINT p;
	p.x = p.y = 0;
	GetClientRect(sysInfo.window, &dst);
	ClientToScreen(sysInfo.window, &p);
	OffsetRect(&dst, p.x, p.y);

	DDBLTFX ddbltfx;
	ddbltfx.dwSize = sizeof(DDBLTFX);
	ddbltfx.dwFillColor = 0;
	res = IDirectDrawSurface_Blt(lpddsprimary, &dst, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);
	src.bottom = PRESCALE_HEIGHT;
	res = IDirectDrawSurface_Blt(lpddsback, &src, 0, 0, DDBLT_COLORFILL | DDBLT_WAIT, &ddbltfx);

	rdp_init();

	return 1;
}

EXPORT m64p_error CALL PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
	/* set version info */
	if (PluginType != NULL)
		*PluginType = M64PLUGIN_GFX;

	if (PluginVersion != NULL)
		*PluginVersion = PLUGIN_VERSION;

	if (APIVersion != NULL)
		*APIVersion = VIDEO_PLUGIN_API_VERSION;

	if (PluginNamePtr != NULL)
		*PluginNamePtr = "Angrylion";

	if (Capabilities != NULL)
	{
		*Capabilities = 0;
	}

	return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                   void (*DebugCallback)(void *, int, const char *))
{
  return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
  return M64ERR_SUCCESS;
}

EXPORT void CALL ReadScreen2(void *dest, int *width, int *height, int front)
{
}

EXPORT void CALL SetRenderingCallback(void (*callback)(int))
{
}

EXPORT void CALL FBRead(unsigned int addr)
{
}

EXPORT void CALL FBWrite(unsigned int addr, unsigned int size)
{
}

EXPORT void CALL FBGetFrameBufferInfo(void *p)
{
}

EXPORT void CALL ResizeVideoOutput(int Width, int Height)
{
}

