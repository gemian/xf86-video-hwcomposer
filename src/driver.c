
/*
 * Copyright 2002, SuSE Linux AG, Author: Egbert Eich
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

/* All drivers initialising the SW cursor need this */
#include "mipointer.h"

/* All drivers using the mi colormap manipulation need this */
#include "micmap.h"

/* identifying atom needed by magnifiers */
#include <X11/Xatom.h>
#include "property.h"

#include "xf86cmap.h"
#include "xf86Crtc.h"

#include "fb.h"

#include "picturestr.h"

/*
 * Driver data structures.
 */
#include "driver.h"
#include "pixmap.h"

#ifdef ENABLE_GLAMOR
#define GLAMOR_FOR_XORG 1
#include <glamor-hybris.h>
#endif
#ifdef ENABLE_DRIHYBRIS
#include <drihybris.h>
#endif
#include <hybris/hwcomposerwindow/hwcomposer.h>

/* These need to be checked */
#include <X11/X.h>
#include <X11/Xproto.h>
#include "scrnintstr.h"
#include "servermd.h"

/* Mandatory functions */
static const OptionInfoRec *	AvailableOptions(int chipid, int busid);
static void     Identify(int flags);
static Bool     Probe(DriverPtr drv, int flags);
static Bool     PreInit(ScrnInfoPtr pScrn, int flags);
static Bool     ScreenInit(SCREEN_INIT_ARGS_DECL);
static Bool     EnterVT(VT_FUNC_ARGS_DECL);
static void     LeaveVT(VT_FUNC_ARGS_DECL);
static Bool     CloseScreen(CLOSE_SCREEN_ARGS_DECL);
static void     FreeScreen(FREE_SCREEN_ARGS_DECL);
static ModeStatus ValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode,
                                 Bool verbose, int flags);
static Bool	SaveScreen(ScreenPtr pScreen, int mode);

/* Internally used functions */
static Bool	hwc_driver_func(ScrnInfoPtr pScrn, xorgDriverFuncOp op,
                            pointer ptr);

#define HWC_VERSION 1
#define HWC_NAME "hwcomposer"
#define HWC_DRIVER_NAME "hwcomposer"

#define HWC_MAJOR_VERSION PACKAGE_VERSION_MAJOR
#define HWC_MINOR_VERSION PACKAGE_VERSION_MINOR
#define HWC_PATCHLEVEL PACKAGE_VERSION_PATCHLEVEL

#define DUMMY_MAX_WIDTH 32767
#define DUMMY_MAX_HEIGHT 32767

// For ~60 FPS
#define TIMER_DELAY 17 /* in milliseconds */

/*
 * This is intentionally screen-independent.  It indicates the binding
 * choice made in the first PreInit.
 */
static int pix24bpp = 0;

//Atom width_mm_atom = 0;
//#define WIDTH_MM_NAME  "WIDTH_MM"
//Atom height_mm_atom = 0;
//#define HEIGHT_MM_NAME "HEIGHT_MM"

/*
 * This contains the functions needed by the server after loading the driver
 * module.  It must be supplied, and gets passed back by the SetupProc
 * function in the dynamic case.  In the static case, a reference to this
 * is compiled in, and this requires that the name of this DriverRec be
 * an upper-case version of the driver name.
 */

_X_EXPORT DriverRec hwcomposer = {
    HWC_VERSION,
    HWC_DRIVER_NAME,
    Identify,
    Probe,
    AvailableOptions,
    NULL,
    0,
    hwc_driver_func
};

static SymTabRec Chipsets[] = {
    { 0, "hwcomposer" },
    { -1,         NULL }
};

#ifdef XFree86LOADER

static MODULESETUPPROTO(Setup);

static XF86ModuleVersionInfo hwcomposerVersRec =
{
	"hwcomposer",
	MODULEVENDORSTRING,
	MODINFOSTRING1,
	MODINFOSTRING2,
	XORG_VERSION_CURRENT,
	HWC_MAJOR_VERSION, HWC_MINOR_VERSION, HWC_PATCHLEVEL,
	ABI_CLASS_VIDEODRV,
	ABI_VIDEODRV_VERSION,
	MOD_CLASS_VIDEODRV,
	{0,0,0,0}
};

/*
 * This is the module init data.
 * Its name has to be the driver name followed by ModuleData
 */
_X_EXPORT XF86ModuleData hwcomposerModuleData = { &hwcomposerVersRec, Setup, NULL };

static pointer
Setup(pointer module, pointer opts, int *errmaj, int *errmin)
{
    static Bool setupDone = FALSE;

    if (!setupDone) {
        setupDone = TRUE;
        xf86AddDriver(&hwcomposer, module, HaveDriverFuncs);

        /*
        * Modules that this driver always requires can be loaded here
        * by calling LoadSubModule().
        */

        /*
        * The return value must be non-NULL on success even though there
        * is no TearDownProc.
        */
        return (pointer)1;
    } else {
        if (errmaj) *errmaj = LDR_ONCEONLY;
            return NULL;
    }
}

#endif /* XFree86LOADER */

/*
 * Build a DisplayModeRec that matches the screen's dimensions.
 *
 * Make up a fake pixel clock so that applications that use the VidMode
 * extension to query the "refresh rate" get 60 Hz.
 */
static void ConstructFakeDisplayMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    mode->HDisplay = mode->HSyncStart = mode->HSyncEnd = mode->HTotal =
        pScrn->virtualX;
    mode->VDisplay = mode->VSyncStart = mode->VSyncEnd = mode->VTotal =
        pScrn->virtualY;
    mode->Clock = mode->HTotal * mode->VTotal * 60 / 1000;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ConstructFakeDisplayMode: virtX: %d, virtY: %d\n", pScrn->virtualX, pScrn->virtualY);

    xf86SetCrtcForModes(pScrn, 0);
}

static Bool
GetRec(ScrnInfoPtr pScrn)
{
    /*
     * Allocate a HWCRec, and hook it into pScrn->driverPrivate.
     * pScrn->driverPrivate is initialised to NULL, so we can check if
     * the allocation has already been done.
     */
    if (pScrn->driverPrivate != NULL)
        return TRUE;

    pScrn->driverPrivate = xnfcalloc(sizeof(HWCRec), 1);

    if (pScrn->driverPrivate == NULL)
        return FALSE;
    return TRUE;
}

static void
FreeRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL)
	return;
    free(pScrn->driverPrivate);
    pScrn->driverPrivate = NULL;
}

static const OptionInfoRec *
AvailableOptions(int chipid, int busid)
{
    return Options;
}

/* Mandatory */
static void
Identify(int flags)
{
    xf86PrintChipsets(HWC_NAME, "Driver for Android devices with HWComposser API",
                      Chipsets);
}

/* Mandatory */
static Bool
Probe(DriverPtr drv, int flags)
{
    Bool foundScreen = FALSE;
    int numDevSections, numUsed;
    GDevPtr *devSections;
    int i;

	xf86DrvMsg(0, X_INFO, "Probe flags: %d\n", flags);

	if (flags & PROBE_DETECT)
        return FALSE;
    /*
     * Find the config file Device sections that match this
     * driver, and return if there are none.
     */
    if ((numDevSections = xf86MatchDevice(HWC_DRIVER_NAME,
                                          &devSections)) <= 0) {
        return FALSE;
    }

    numUsed = numDevSections;

    if (numUsed > 0) {
        for (i = 0; i < numUsed; i++) {
            ScrnInfoPtr pScrn = NULL;
            int entityIndex =
            xf86ClaimNoSlot(drv, 0, devSections[i], TRUE);
            /* Allocate a ScrnInfoRec and claim the slot */
            if ((pScrn = xf86AllocateScreen(drv, 0))) {
            xf86AddEntityToScreen(pScrn,entityIndex);
                pScrn->driverVersion = HWC_VERSION;
                pScrn->driverName    = HWC_DRIVER_NAME;
                pScrn->name          = HWC_NAME;
                pScrn->Probe         = Probe;
                pScrn->PreInit       = PreInit;
                pScrn->ScreenInit    = ScreenInit;
                pScrn->SwitchMode    = SwitchMode;
                pScrn->AdjustFrame   = AdjustFrame;
                pScrn->EnterVT       = EnterVT;
                pScrn->LeaveVT       = LeaveVT;
                pScrn->FreeScreen    = FreeScreen;
                pScrn->ValidMode     = ValidMode;

                foundScreen = TRUE;
            }
        }
    }

    free(devSections);

    return foundScreen;
}

static void
try_enable_drihybris(ScrnInfoPtr pScrn)
{
#ifdef ENABLE_DRIHYBRIS
    HWCPtr hwc = HWCPTR(pScrn);
#ifndef __ANDROID__
    if (xf86LoadSubModule(pScrn, "drihybris"))
#endif
    {
        hwc->drihybris = TRUE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "drihybris initialized\n");
    }
#endif
}

static void
try_enable_glamor(ScrnInfoPtr pScrn)
{
#ifdef ENABLE_GLAMOR
    HWCPtr hwc = HWCPTR(pScrn);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "try_enable_glamor\n");

    try_enable_drihybris(pScrn);

#ifndef __ANDROID__
    if (xf86LoadSubModule(pScrn, GLAMOR_EGLHYBRIS_MODULE_NAME)) {
#endif // __ANDROID__
        if (hwc_glamor_egl_init(pScrn, hwc->egl_display,
                hwc->primary_display.hwc_renderer.context, EGL_NO_SURFACE)) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor-hybris primary initialized\n");
            hwc->glamor = TRUE;
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "glamor-hybris primary initialization failed\n");
        }
//		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "try_enable_glamor - external\n");
//        if (hwc_glamor_egl_init(pScrn, hwc->egl_display,
//                hwc->external_display.hwc_renderer.context, EGL_NO_SURFACE)) {
//            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor-hybris external initialized\n");
//            hwc->glamor = TRUE;
//        } else {
//            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
//                       "glamor-hybris external initialization failed\n");
//        }

#ifndef __ANDROID__
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to load glamor-hybris module.\n");
    }
#endif // __ANDROID__
#endif // ENABLE_GLAMOR
}

# define RETURN \
    { FreeRec(pScrn);\
			    return FALSE;\
					     }

void hwc_set_egl_platform(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);
    const char *egl_platform_str = xf86GetOptValString(hwc->Options,
                                                    OPTION_EGL_PLATFORM);
    if (egl_platform_str) {
        setenv("EGL_PLATFORM", egl_platform_str, 1);
    }
    else {
        // Default to EGL_PLATFORM=hwcomposer
        setenv("EGL_PLATFORM", "hwcomposer", 0);
    }
}

static int read_int_from_file(ScrnInfoPtr pScrn, const char *filename) {
    FILE *file;
    char state[256] = "", *ret;

    file = fopen(filename, "r");
    if (!file) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "read_int_from_file failed fopen file: %p\n", file);
        return -1;
    }

    ret = fgets(state, sizeof(state)-1, file);
    fclose(file);

    if (!ret) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "read_int_from_file failed ret: %s\n", ret);
        return -1;
    }

    char *end;
    const long state_long = strtol(state, &end, 10);
    if (state == end) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "strtol failed end: %s\n", end);
        return -1;
    }

    return (int)state_long;
}

/* Mandatory */
Bool
PreInit(ScrnInfoPtr pScrn, int flags)
{
    HWCPtr hwc;
	EntityInfoPtr entityInfoPtr = xf86GetEntityInfo(pScrn->entityList[0]);
    GDevPtr device = entityInfoPtr->device;
    const char *s;
    const char *accel_method_str;
    Bool do_glamor;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PreInit flags:%d, numEntities: %d\n", flags, pScrn->numEntities);

    if (flags & PROBE_DETECT)
        return TRUE;

    /* Allocate the HWCRec driverPrivate */
    if (!GetRec(pScrn)) {
        return FALSE;
    }

    hwc = HWCPTR(pScrn);
    pScrn->monitor = pScrn->confScreen->monitor;

    hwc->device_open = read_int_from_file(pScrn, "/sys/class/switch/hall/state");
    hwc->usb_hdmi_plugged = read_int_from_file(pScrn, "/sys/class/switch/usb_hdmi/state");
    if (hwc->usb_hdmi_plugged) {
        hdmi_power_enable(FALSE);
        hdmi_enable(FALSE);
    }

    if (!xf86SetDepthBpp(pScrn, 0, 0, 0,  Support24bppFb | Support32bppFb))
        return FALSE;
    else {
        /* Check that the returned depth is one we support */
        switch (pScrn->depth) {
        case 8:
        case 15:
        case 16:
        case 24:
        case 30:
            break;
        default:
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Given depth (%d) is not supported by this driver\n",
                       pScrn->depth);
            return FALSE;
        }
    }

    xf86PrintDepthBpp(pScrn);
    if (pScrn->depth == 8)
        pScrn->rgbBits = 8;

    /* Get the depth24 pixmap format */
    if (pScrn->depth == 24 && pix24bpp == 0)
        pix24bpp = xf86GetBppFromDepth(pScrn, 24);

    /*
     * This must happen after pScrn->display has been set because
     * xf86SetWeight references it.
     */
    if (pScrn->depth > 8) {
        /* The defaults are OK for us */
        rgb zeros = {0, 0, 0};

        if (!xf86SetWeight(pScrn, zeros, zeros)) {
            return FALSE;
        } else {
            /* XXX check that weight returned is supported */
            ;
        }
    }

    if (!xf86SetDefaultVisual(pScrn, -1))
        return FALSE;

    if (pScrn->depth > 1) {
    Gamma zeros = {0.0, 0.0, 0.0};

    if (!xf86SetGamma(pScrn, zeros))
        return FALSE;
    }

    xf86CollectOptions(pScrn, device->options);
    /* Process the options */
    if (!(hwc->Options = malloc(sizeof(Options))))
        return FALSE;
    memcpy(hwc->Options, Options, sizeof(Options));

    xf86ProcessOptions(pScrn->scrnIndex, pScrn->options, hwc->Options);

    hwc->swCursor = xf86ReturnOptValBool(hwc->Options, OPTION_SW_CURSOR, FALSE);
    if (hwc->swCursor) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hardware cursor disabled\n");
    }

    hwc_set_egl_platform(pScrn);

    if (!hwc_hwcomposer_init(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize HWComposer API and layers\n");
        return FALSE;
    }

    if (!hwc_lights_init(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "failed to initialize lights module for backlight control\n");
    }

    hwc->udev_switches.pScrn = pScrn;
    init_udev_switches(&hwc->udev_switches);
    hwc->primary_display.dpmsMode = DPMSModeOff;
    hwc->external_display.dpmsMode = DPMSModeOff;

    hwc_display_pre_init(pScrn); //xf86CrtcConfigInit + xf86CrtcSetSizeRange + crtc's + output's + xf86InitialConfiguration

    xf86SetDpi(pScrn, 0, 0);

#ifndef __ANDROID__
    if (xf86LoadSubModule(pScrn, "fb") == NULL) {
        RETURN;
    }
#endif // __ANDROID__

    if (!hwc->swCursor) {
#ifndef __ANDROID__
        if (!xf86LoadSubModule(pScrn, "ramdac"))
            RETURN;
#endif // __ANDROID__
    }

    /* We have no contiguous physical fb in physical memory */
    pScrn->memPhysBase = 0;
    pScrn->fbOffset = 0;

    accel_method_str = xf86GetOptValString(hwc->Options, OPTION_ACCEL_METHOD);
    do_glamor = (!accel_method_str ||
                      strcmp(accel_method_str, "glamor") == 0);

    if (!do_glamor) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
    }

	if (!hwc_egl_renderer_init(pScrn, do_glamor)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "failed to initialize primary EGL renderer\n");
		return FALSE;
	}

    if (!hwc_init_hybris_native_buffer(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize libhybris native buffer EGL extension\n");
        return FALSE;
    }

    hwc->glamor = FALSE;
    hwc->drihybris = FALSE;

	if (do_glamor) {
		try_enable_glamor(pScrn);
	}
	if (!hwc->glamor) {
		try_enable_drihybris(pScrn);

		if (hwc->drihybris) {
			// Force RGBA
			pScrn->mask.red = 0xff;
			pScrn->mask.blue = 0xff0000;
			pScrn->offset.red = 0;
			pScrn->offset.blue = 16;

			if (!xf86SetDefaultVisual(pScrn, -1)) {
				xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to xf86SetDefaultVisual.\n");
				return FALSE;
			}
		}
	}

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PreInit finished\n");

    return TRUE;
}
#undef RETURN

/* Mandatory */
static Bool
EnterVT(VT_FUNC_ARGS_DECL)
{
	xf86DrvMsg(0, X_INFO, "EnterVT\n");
	return TRUE;
}

/* Mandatory */
static void
LeaveVT(VT_FUNC_ARGS_DECL)
{
	xf86DrvMsg(0, X_INFO, "LeaveVT\n");
}

static void
LoadPalette(
   ScrnInfoPtr pScrn,
   int numColors,
   int *indices,
   LOCO *colors,
   VisualPtr pVisual
){
   int i, index, shift, Gshift;
   HWCPtr hwc = HWCPTR(pScrn);
   xf86DrvMsg(pScrn->scrnIndex, X_INFO, "LoadPalette\n");

   switch(pScrn->depth) {
   case 15:
    shift = Gshift = 1;
    break;
   case 16:
    shift = 0;
    Gshift = 0;
    break;
   default:
    shift = Gshift = 0;
    break;
   }

   for(i = 0; i < numColors; i++) {
       index = indices[i];
       hwc->colors[index].red = colors[index].red << shift;
       hwc->colors[index].green = colors[index].green << Gshift;
       hwc->colors[index].blue = colors[index].blue << shift;
   }
}

void triggerGlamourEglFlush(HWCPtr hwc) {
    /* create EGL sync object so renderer thread could wait for
     * glamor to flush commands pipeline
     * (just glFlush which is called in glamor's blockHandler
     * is not enough on Mali to get the buffer updated) */
    if (hwc->egl_fence != EGL_NO_SYNC_KHR) {
        EGLSyncKHR fence = hwc->egl_fence;
        hwc->egl_fence = EGL_NO_SYNC_KHR;
        eglDestroySyncKHR(hwc->egl_display, fence);
    }
    hwc->egl_fence = eglCreateSyncKHR(hwc->egl_display, EGL_SYNC_FENCE_KHR, NULL);
    /* make sure created sync object eventually signals */
    glFlush();
}

void checkDamageAndTriggerRedraw(ScrnInfoPtr pScrn) {
	HWCPtr hwc = HWCPTR(pScrn);

	if (hwc->damage && (hwc->primary_display.dpmsMode == DPMSModeOn || hwc->external_display.dpmsMode == DPMSModeOn)) {
        RegionPtr dirty = DamageRegion(hwc->damage);
        unsigned num_cliprects = REGION_NUM_RECTS(dirty);

        if (num_cliprects) {
            DamageEmpty(hwc->damage);
            if (hwc->glamor) {
                triggerGlamourEglFlush(hwc);
            }
            hwc_trigger_redraw(pScrn);
        }
    }
}

static void hwcBlockHandler(ScreenPtr pScreen, void *timeout) {
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	HWCPtr hwc = HWCPTR(pScrn);

	pScreen->BlockHandler = hwc->BlockHandler;
	pScreen->BlockHandler(pScreen, timeout);
	pScreen->BlockHandler = hwcBlockHandler;

//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwcBlockHandler\n");

    checkDamageAndTriggerRedraw(pScrn);
}

Bool AdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height);

static Bool
CreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    PixmapPtr rootPixmap;
    Bool ret;
    void *pixels = NULL;
    int err;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CreateScreenResources width:%d, height:%d, virtX: %d, virtY: %d\n", pScreen->width, pScreen->height, pScrn->virtualX, pScrn->virtualY);

    pScreen->CreateScreenResources = hwc->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = CreateScreenResources;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CreateScreenResources upstream created - ret %d\n", ret);

//    xf86CrtcPtr crtc = hwc_display->pCrtc;
//    HWCPtr hwc = HWCPTR(crtc->scrn);
//    int width = xf86ModeWidth(&crtc->mode, crtc->rotation);
//    int height = xf86ModeHeight(&crtc->mode, crtc->rotation);
    ret = AdjustScreenPixmap(pScrn, pScreen->width, pScreen->height);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CreateScreenResources adjusted screen pixmap - ret %d\n", ret);

    hwc->rendererIsRunning = 1;

    if (pthread_mutex_init(&(hwc->rendererLock), NULL) ||
        pthread_mutex_init(&(hwc->dirtyLock), NULL) ||
        pthread_cond_init(&(hwc->dirtyCond), NULL) ||
        pthread_create(&(hwc->rendererThread), NULL, hwc_egl_renderer_thread, pScreen)) {
        FatalError("Error creating rendering thread\n");
    }

    return ret;
}

static void checkDirtyAndRenderUpdate(ScrnInfoPtr pScrn) {
    HWCPtr hwc = HWCPTR(pScrn);
    PixmapPtr rootPixmap;
    int err;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "checkDirtyAndRenderUpdate dirty: %d, pDpms: %d, eDpms: %d\n", hwc->dirty, hwc->primary_display.dpmsMode, hwc->external_display.dpmsMode);

    if (hwc->dirty && (hwc->primary_display.dpmsMode == DPMSModeOn || hwc->external_display.dpmsMode == DPMSModeOn)) {
        void *pixels = NULL;
        rootPixmap = pScrn->pScreen->GetScreenPixmap(pScrn->pScreen);
        hwc->egl_proc.eglHybrisUnlockNativeBuffer(hwc->buffer);

        if (hwc->primary_display.dpmsMode == DPMSModeOn) {
            hwc_egl_renderer_update(pScrn->pScreen, &hwc->primary_display);
        }
        if (hwc->external_display.dpmsMode == DPMSModeOn) {
            hwc_egl_renderer_update(pScrn->pScreen, &hwc->external_display);
        }

        err = hwc->egl_proc.eglHybrisLockNativeBuffer(hwc->buffer,
                        HYBRIS_USAGE_SW_READ_OFTEN|HYBRIS_USAGE_SW_WRITE_OFTEN,
                        0, 0, hwc->stride, hwc->height, &pixels);

        if (!hwc->glamor) {
            if (!pScrn->pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
                FatalError("Couldn't adjust screen pixmap\n");
        }

        hwc->dirty = FALSE;
    }
}

static CARD32 hwc_update_by_timer(OsTimerPtr timer, CARD32 time, void *ptr) {
    ScreenPtr pScreen = (ScreenPtr) ptr;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

    checkDirtyAndRenderUpdate(pScrn);

    return TIMER_DELAY;
}

/* Mandatory */
static Bool
ScreenInit(SCREEN_INIT_ARGS_DECL)
{
    ScrnInfoPtr pScrn;
    HWCPtr hwc;
    int ret;
    VisualPtr visual;

    /*
     * we need to get the ScrnInfoRec for this screen, so let's allocate
     * one first thing
     */
    pScrn = xf86ScreenToScrn(pScreen);
    hwc = HWCPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit myNum: %d, dpi: %d, displayWidth: %d\n", pScreen->myNum,pScrn->xDpi,pScrn->displayWidth);

    /*
     * Reset visual list.
     */
    miClearVisualTypes();

    /* Setup the visuals we support. */

    if (!miSetVisualTypes(pScrn->depth,
      		      miGetDefaultVisualMask(pScrn->depth),
		      pScrn->rgbBits, pScrn->defaultVisual))
         return FALSE;

    if (!miSetPixmapDepths ()) return FALSE;

    /*
     * Call the framebuffer layer's ScreenInit function, and fill in other
     * pScreen fields.
     */
    ret = fbScreenInit(pScreen, NULL,
                       pScrn->virtualX, pScrn->virtualY,
                       pScrn->xDpi, pScrn->yDpi,
                       pScrn->displayWidth, pScrn->bitsPerPixel);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit fbScreenInit ret: %d, virtualX: %d, virtualY: %d\n", ret, pScrn->virtualX, pScrn->virtualY);
    if (!ret) {
        return FALSE;
    }

    if (pScrn->depth > 8) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit Fixup RGB ordering\n");
        /* Fixup RGB ordering */
        visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue = pScrn->offset.blue;
                visual->redMask = pScrn->mask.red;
                visual->greenMask = pScrn->mask.green;
                visual->blueMask = pScrn->mask.blue;
            }
        }
    }

    /* must be after RGB ordering fixed */
    fbPictureInit(pScreen, 0, 0);

#ifdef ENABLE_GLAMOR
    if (hwc->glamor && !glamor_init(pScreen, GLAMOR_USE_EGL_SCREEN)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize glamor at ScreenInit() time.\n");
        return FALSE;
    }
#endif

	xf86SetBlackWhitePixels(pScreen);

    hwc->CreateScreenResources = pScreen->CreateScreenResources;
    pScreen->CreateScreenResources = CreateScreenResources;

    xf86SetBackingStore(pScreen);
    xf86SetSilkenMouse(pScreen);

    /* Initialise cursor functions */
    miDCInitialize (pScreen, xf86GetPointerScreenFuncs());

    /* Need to extend HWcursor support to handle mask interleave */
    if (!hwc->swCursor) {
        hwc->cursorWidth = 64;
        hwc->cursorHeight = 64;

        xf86_cursors_init(pScreen, hwc->cursorWidth, hwc->cursorHeight,
                          HARDWARE_CURSOR_UPDATE_UNHIDDEN |
                          HARDWARE_CURSOR_ARGB);
    }

    /* Initialise default colourmap */
    if(!miCreateDefColormap(pScreen))
        return FALSE;

    if (!xf86HandleColormaps(pScreen, 1024, pScrn->rgbBits,
                             LoadPalette, NULL,
                             CMAP_PALETTED_TRUECOLOR
                             | CMAP_RELOAD_ON_MODE_SWITCH))
        return FALSE;

    // Initialise randr 1.2 mode-setting functions and set first mode.
    // Note that the mode won't be usable until the server has resized the
    // framebuffer to something reasonable.
    if (!xf86CrtcScreenInit(pScreen)) {
        return FALSE;
    }
    if (!xf86SetDesiredModes(pScrn)) {
        return FALSE;
    }

	pScreen->SaveScreen = SaveScreen;
	
    /* Wrap the current CloseScreen function */
    hwc->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = CloseScreen;
	
    xf86DPMSInit(pScreen, xf86DPMSSet, 0);

    if (hwc->glamor) {
        XF86VideoAdaptorPtr     glamor_adaptor;

        glamor_adaptor = glamor_xv_init(pScreen, 16);
        if (glamor_adaptor != NULL)
            xf86XVScreenInit(pScreen, &glamor_adaptor, 1);
        else
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to initialize XV support.\n");
    } else {
        if (hwc->drihybris) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Initializing drihybris.\n");
            hwc_drihybris_screen_init(pScreen);
        }
        hwc_pixmap_init(pScreen);
    }

    /* Report any unused options (only for the first generation) */
    if (serverGeneration == 1) {
        xf86ShowUnusedOptions(pScrn->scrnIndex, pScrn->options);
    }
	
    /* Wrap the current BlockHandler function */
    hwc->BlockHandler = pScreen->BlockHandler;
    pScreen->BlockHandler = hwcBlockHandler;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit End - randr_crtc: %d\n",hwc->primary_display.pCrtc->randr_crtc->rotations);

#ifdef ENABLE_DRIHYBRIS
    if (hwc->drihybris) {
        drihybris_extension_init();
    }
#endif
    if (!hwc_present_screen_init(pScreen)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize the Present extension.\n");
    }



    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit End\n");
    return TRUE;
}

/* Mandatory */
Bool
SwitchMode(SWITCH_MODE_ARGS_DECL)
{
	xf86DrvMsg(0, X_INFO, "SwitchMode\n");
    return TRUE;
}

/* Mandatory */
void
AdjustFrame(ADJUST_FRAME_ARGS_DECL)
{
	xf86DrvMsg(0, X_INFO, "AdjustFrame\n");
}

/* Mandatory */
static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CloseScreen\n");

    TimerCancel(hwc->timer);

    if (hwc->damage) {
        DamageUnregister(hwc->damage);
        DamageDestroy(hwc->damage);
        hwc->damage = NULL;
    }

    hwc->rendererIsRunning = 0;
	hwc_trigger_redraw(pScrn);

    pthread_join(hwc->rendererThread, NULL);
    pthread_mutex_destroy(&(hwc->rendererLock));
    pthread_mutex_destroy(&(hwc->dirtyLock));
    pthread_cond_destroy(&(hwc->dirtyCond));

    if (hwc->buffer != NULL) {
        hwc->egl_proc.eglHybrisUnlockNativeBuffer(hwc->buffer);
        hwc->egl_proc.eglHybrisReleaseNativeBuffer(hwc->buffer);
        hwc->buffer = NULL;
    }

    if (hwc->primary_display.win  != NULL) {
        HWCNativeWindowDestroy(hwc->primary_display.win);
        hwc->primary_display.win = NULL;
    }
    if (hwc->external_display.win != NULL) {
        HWCNativeWindowDestroy(hwc->external_display.win);
        hwc->external_display.win = NULL;
    }

    if (hwc->CursorInfo)
        xf86DestroyCursorInfoRec(hwc->CursorInfo);

    pScrn->vtSema = FALSE;
    pScreen->CloseScreen = hwc->CloseScreen;
    return (*pScreen->CloseScreen)(CLOSE_SCREEN_ARGS);
}

/* Optional */
static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
    SCRN_INFO_PTR(arg);
    FreeRec(pScrn);
}

static Bool
SaveScreen(ScreenPtr pScreen, int mode)
{
    return TRUE;
}

/* Optional */
static ModeStatus
ValidMode(SCRN_ARG_TYPE arg, DisplayModePtr mode, Bool verbose, int flags)
{
    return(MODE_OK);
}

#ifndef HW_SKIP_CONSOLE
#define HW_SKIP_CONSOLE 4
#endif

static Bool
hwc_driver_func(ScrnInfoPtr pScrn, xorgDriverFuncOp op, pointer ptr)
{
    CARD32 *flag;

    switch (op) {
    case GET_REQUIRED_HW_INTERFACES:
        flag = (CARD32*)ptr;
        (*flag) = HW_SKIP_CONSOLE;
        return TRUE;
    default:
        return FALSE;
    }
}
