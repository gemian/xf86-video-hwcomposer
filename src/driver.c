
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

Atom width_mm_atom = 0;
#define WIDTH_MM_NAME  "WIDTH_MM"
Atom height_mm_atom = 0;
#define HEIGHT_MM_NAME "HEIGHT_MM"

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

typedef enum {
    OPTION_ACCEL_METHOD,
    OPTION_EGL_PLATFORM,
    OPTION_SW_CURSOR,
    OPTION_ROTATE
} Opts;

static const OptionInfoRec Options[] = {
    { OPTION_ACCEL_METHOD, "AccelMethod", OPTV_STRING, {0}, FALSE},
    { OPTION_EGL_PLATFORM, "EGLPlatform", OPTV_STRING, {0}, FALSE},
    { OPTION_SW_CURSOR,     "SWcursor",    OPTV_BOOLEAN,{0}, FALSE},
    { OPTION_ROTATE,       "Rotate",      OPTV_STRING, {0}, FALSE },
    { -1,               NULL,       OPTV_NONE,    {0}, FALSE }
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


/************************
 * XRANDR support begin *
 ************************/

static Bool dummy_config_resize(ScrnInfoPtr pScrn, int cw, int ch);
static Bool DUMMYAdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height);

static const xf86CrtcConfigFuncsRec DUMMYCrtcConfigFuncs = {
    .resize = dummy_config_resize
};


static void
hwc_crtc_dpms(xf86CrtcPtr crtc, int mode)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_crtc_dpms mode: %d, index: %d\n", mode, index);

	//handled in hwc_output_dpms
}

static Bool
dummy_crtc_lock (xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_lock index: %d\n", index);
    //other folk wait for GPU idle here
    return FALSE;
}

static Bool
dummy_crtc_mode_fixup (xf86CrtcPtr crtc, DisplayModePtr mode,
                              DisplayModePtr adjusted_mode)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_mode_fixup mode: %d, adjusted_mode: %d, index: %d\n", mode, adjusted_mode, index);
    return TRUE;
}

static void
dummy_crtc_prepare (xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_prepare index:%d\n", index);
}

static void
dummy_crtc_commit (xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_commit index:%d\n", index);
}

static void
dummy_crtc_destroy (xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_destroy index: %d\n", index);
}

static void
dummy_crtc_gamma_set (xf86CrtcPtr crtc, CARD16 *red,
                             CARD16 *green, CARD16 *blue, int size)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_gamma_set size: %d, index: %d\n", size, index);
}

static void *
dummy_crtc_shadow_allocate (xf86CrtcPtr crtc, int width, int height)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_shadow_allocate width: %d, height: %d, index: %d\n", width, height, index);
    return NULL;
}

static void
dummy_crtc_mode_set (xf86CrtcPtr crtc, DisplayModePtr mode,
                            DisplayModePtr adjusted_mode, int x, int y)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
   	int index = (int64_t)crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_mode_set mode: %p, adjusted_mode: %p, x: %d, y: %d, index: %d\n", mode, adjusted_mode, x, y, index);
}

static Bool
hwc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;
	int index = (int64_t)crtc->driver_private;
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_mode_major index: %d\n", index);

    return TRUE;
}
static void
hwc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_cursor_colors index: %d\n", index);

}

static void
hwc_set_cursor_position(xf86CrtcPtr crtc, int x, int y)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorX = x;
    hwc->cursorY = y;
    hwc_trigger_redraw(crtc->scrn);
}

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by uploading it to texture.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
static Bool
hwc_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);

    glBindTexture(GL_TEXTURE_2D, hwc->renderer.cursorTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hwc->cursorWidth, hwc->cursorHeight,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    hwc_trigger_redraw(crtc->scrn);
    return TRUE;
}

static void
hwc_hide_cursor(xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorShown = FALSE;
    hwc_trigger_redraw(crtc->scrn);
}

static void
hwc_show_cursor(xf86CrtcPtr crtc)
{
    HWCPtr hwc = HWCPTR(crtc->scrn);
    hwc->cursorShown = TRUE;
    hwc_trigger_redraw(crtc->scrn);
}

static const xf86CrtcFuncsRec DUMMYCrtcFuncs = {
    .dpms = hwc_crtc_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .lock = dummy_crtc_lock,
    .unlock = NULL, /* This will not be invoked if lock returns FALSE. */
    .mode_fixup = dummy_crtc_mode_fixup,
    .prepare = dummy_crtc_prepare,
    .mode_set = dummy_crtc_mode_set,
    .commit = dummy_crtc_commit,
    .gamma_set = dummy_crtc_gamma_set,
    .shadow_allocate = dummy_crtc_shadow_allocate,
    .shadow_create = NULL, /* These two should not be invoked if allocate
                              returns NULL. */
    .shadow_destroy = NULL,
    .set_cursor_colors = NULL,
    .set_cursor_position = NULL,
    .show_cursor = NULL,
    .hide_cursor = NULL,
    .load_cursor_argb = NULL,
    .destroy = dummy_crtc_destroy,
    .set_mode_major = hwc_set_mode_major,
    .set_cursor_colors = hwc_set_cursor_colors,
    .set_cursor_position = hwc_set_cursor_position,
    .show_cursor = hwc_show_cursor,
    .hide_cursor = hwc_hide_cursor,
    .load_cursor_argb_check = hwc_load_cursor_argb_check
};

static void
dummy_output_prepare (xf86OutputPtr output)
{
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_prepare index: %d\n", index);
}

static void
dummy_output_commit (xf86OutputPtr output)
{
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_commit index: %d\n", index);
}

static void
dummy_output_destroy (xf86OutputPtr output)
{
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_destroy index: %d\n", index);
}

static void
hwc_output_dpms (xf86OutputPtr output, int mode)
{
    HWCPtr hwc = HWCPTR(output->scrn);
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_dpms mode: %d, index: %d\n", mode, index);

    ScrnInfoPtr pScrn;
    pScrn = output->scrn;

    if (index == 0) {
    	hwc->dpmsMode = mode;
    }

    if (index == 1 && !(hwc->connected_outputs & (1 << index))) {
    	return;
    }

    if (mode != DPMSModeOn)
    {
        // Wait for the renderer thread to finish to avoid causing locks
        pthread_mutex_lock(&(hwc->dirtyLock));
        hwc->dirty = FALSE;
        pthread_mutex_unlock(&(hwc->dirtyLock));
    }

    pthread_mutex_lock(&(hwc->rendererLock));
    hwc_set_power_mode(pScrn, (index == 0) ? HWC_DISPLAY_PRIMARY : HWC_DISPLAY_EXTERNAL, (mode == DPMSModeOn) ? 1 : 0);
    pthread_mutex_unlock(&(hwc->rendererLock));

    hwc_toggle_screen_brightness(pScrn);

    if (mode == DPMSModeOn)
        // Force redraw after unblank
        hwc_trigger_redraw(pScrn);
}

static int
dummy_output_mode_valid (xf86OutputPtr output, DisplayModePtr mode)
{
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_valid mode: %p, index: %d\n", mode, index);
    return MODE_OK;
}

static Bool
dummy_output_mode_fixup (xf86OutputPtr output, DisplayModePtr mode,
        DisplayModePtr adjusted_mode)
{
	int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_fixup mode: %p, adjusted_mode: %p, index: %d\n", mode, adjusted_mode, index);
    return TRUE;
}

static void
dummy_output_mode_set (xf86OutputPtr output, DisplayModePtr mode,
        DisplayModePtr adjusted_mode)
{
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t)output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_set mode: %p, adjusted_mode: %p, index: %d\n", mode, adjusted_mode, index);

    /* set to connected at first mode set */
    hwc->connected_outputs |= 1 << index;
}

/* The first virtual monitor is always connected. Others only after setting its
 * mode */
static xf86OutputStatus
hwc_output_detect (xf86OutputPtr output)
{
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t)output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_detect index: %d\n", index);

    if (hwc->connected_outputs & (1 << index))
        return XF86OutputStatusConnected;
    else
        return XF86OutputStatusDisconnected;
}

static DisplayModePtr
hwc_output_get_modes (xf86OutputPtr output)
{
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t)output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_get_modes index: %d\n", index);

    if (index == 0) {
    	return xf86DuplicateModes(NULL, hwc->modes);
	} else {
	    DisplayModePtr pModes = NULL, pMode, pModeSrc;

    	/* copy modes from config */
    	for (pModeSrc = output->scrn->modes; pModeSrc; pModeSrc = pModeSrc->next)
    	{
        	pMode = xnfcalloc(1, sizeof(DisplayModeRec));
            memcpy(pMode, pModeSrc, sizeof(DisplayModeRec));
            pMode->next = NULL;
            pMode->prev = NULL;
            pMode->name = strdup(pModeSrc->name);
            pModes = xf86ModesAdd(pModes, pMode);
            if (pModeSrc->next == output->scrn->modes)
                break;
    	}
    	return pModes;
    }
}

void dummy_output_register_prop(xf86OutputPtr output, Atom prop, uint64_t value)
{
    INT32 dims_range[2] = { 0, 65535 };
    int err;

    err = RRConfigureOutputProperty(output->randr_output, prop, FALSE,
            TRUE, FALSE, 2, dims_range);
    if (err != 0)
        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                                       "RRConfigureOutputProperty error, %d\n", err);

    err = RRChangeOutputProperty(output->randr_output, prop, XA_INTEGER,
            32, PropModeReplace, 1, &value, FALSE, FALSE);
    if (err != 0)
        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
                "RRChangeOutputProperty error, %d\n", err);
}

void dummy_output_create_resources(xf86OutputPtr output)
{
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t)output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_create_resources %d\n", index);

    if (!ValidAtom(width_mm_atom))
        width_mm_atom = MakeAtom(WIDTH_MM_NAME, strlen(WIDTH_MM_NAME), 1);
    if (!ValidAtom(height_mm_atom))
        height_mm_atom = MakeAtom(HEIGHT_MM_NAME, strlen(HEIGHT_MM_NAME), 1);

    dummy_output_register_prop(output, width_mm_atom, 0);
    dummy_output_register_prop(output, height_mm_atom, 0);
}

static Bool dummy_output_set_property(xf86OutputPtr output, Atom property,
        RRPropertyValuePtr value)
{
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_set_property prop: %d, valT: %d\n", property, value->type);

    if (property == width_mm_atom || property == height_mm_atom) {
        INT32 val;

        if (value->type != XA_INTEGER || value->format != 32 ||
                value->size != 1)
        {
            return FALSE;
        }

        val = *(INT32 *)value->data;
        if (property == width_mm_atom)
            output->mm_width = val;
        else if (property == height_mm_atom)
            output->mm_height = val;
        return TRUE;
    }
    return TRUE;
}

static const xf86OutputFuncsRec DUMMYOutputFuncs = {
    .create_resources = dummy_output_create_resources,
    .dpms = hwc_output_dpms,
    .save = NULL, /* These two are never called by the server. */
    .restore = NULL,
    .mode_valid = dummy_output_mode_valid,
    .mode_fixup = dummy_output_mode_fixup,
    .prepare = dummy_output_prepare,
    .commit = dummy_output_commit,
    .mode_set = dummy_output_mode_set,
    .detect = hwc_output_detect,
    .get_modes = hwc_output_get_modes,
#ifdef RANDR_12_INTERFACE
    .set_property = dummy_output_set_property,
#endif
    .destroy = dummy_output_destroy
};

static Bool
dummy_config_resize(ScrnInfoPtr pScrn, int cw, int ch)
{
    if (!pScrn->vtSema) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "We do not own the active VT, exiting.\n");
        return TRUE;
    }
    return DUMMYAdjustScreenPixmap(pScrn, cw, ch);
}

Bool DUMMYAdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    uint64_t cbLine = (width * xf86GetBppFromDepth(pScrn, pScrn->depth) / 8 + 3) & ~3;
    int displayWidth = cbLine * 8 / xf86GetBppFromDepth(pScrn, pScrn->depth);

    if (   width == pScrn->virtualX
            && height == pScrn->virtualY
            && displayWidth == pScrn->displayWidth)
        return TRUE;
    if (!pPixmap) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "Failed to get the screen pixmap.\n");
        return FALSE;
    }
    if (cbLine > UINT32_MAX || cbLine * height >= pScrn->videoRam * 1024)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                "Unable to set up a virtual screen size of %dx%d with %d Kb of video memory available.  Please increase the video memory size.\n",
                width, height, pScrn->videoRam);
        return FALSE;
    }
    pScreen->ModifyPixmapHeader(pPixmap, width, height,
            pScrn->depth, xf86GetBppFromDepth(pScrn, pScrn->depth), cbLine,
            pPixmap->devPrivate.ptr);
    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = displayWidth;

    return TRUE;
}

/**********************
 * XRANDR support end *
 **********************/

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

    try_enable_drihybris(pScrn);

#ifndef __ANDROID__
    if (xf86LoadSubModule(pScrn, GLAMOR_EGLHYBRIS_MODULE_NAME)) {
#endif // __ANDROID__
        if (hwc_glamor_egl_init(pScrn, hwc->renderer.display,
                hwc->renderer.context, EGL_NO_SURFACE)) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor-hybris initialized\n");
            hwc->glamor = TRUE;
        } else {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "glamor-hybris initialization failed\n");
        }
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

/* Mandatory */
Bool
PreInit(ScrnInfoPtr pScrn, int flags)
{
    HWCPtr hwc;
    GDevPtr device = xf86GetEntityInfo(pScrn->entityList[0])->device;
    xf86CrtcPtr crtc;
    xf86OutputPtr output;
    const char *s;
    const char *accel_method_str;
    Bool do_glamor;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PreInit flags:%d\n", flags);

    if (flags & PROBE_DETECT)
        return TRUE;

    /* Allocate the HWCRec driverPrivate */
    if (!GetRec(pScrn)) {
        return FALSE;
    }

    hwc = HWCPTR(pScrn);
    pScrn->monitor = pScrn->confScreen->monitor;

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

    /* rotation */
    hwc->rotation = HWC_ROTATE_NORMAL;
    if ((s = xf86GetOptValString(hwc->Options, OPTION_ROTATE)))
    {
        if(!xf86NameCmp(s, "CW")) {
            hwc->rotation = HWC_ROTATE_CW;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen clockwise\n");
        }
        else if(!xf86NameCmp(s, "UD")) {
            hwc->rotation = HWC_ROTATE_UD;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen upside-down\n");
        }
        else if(!xf86NameCmp(s, "CCW")) {
            hwc->rotation = HWC_ROTATE_CCW;
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen counter-clockwise\n");
        }
        else {
            xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                    "\"%s\" is not a valid value for Option \"Rotate\"\n", s);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "valid options are \"CW\", \"UD\", \"CCW\"\n");
        }
    }

    hwc->swCursor = xf86ReturnOptValBool(hwc->Options, OPTION_SW_CURSOR, FALSE);
    if (hwc->swCursor) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                    "hardware cursor disabled\n");
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

	// bitmask
	hwc->connected_outputs = 1;

	hwc_display_pre_init(pScrn, &crtc, &output);

    /* If monitor resolution is set on the command line, use it */
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

    accel_method_str = xf86GetOptValString(hwc->Options,
                                                       OPTION_ACCEL_METHOD);
    do_glamor = (!accel_method_str ||
                      strcmp(accel_method_str, "glamor") == 0);

    if (!do_glamor) {
        xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
    }

    if (!hwc_egl_renderer_init(pScrn, do_glamor)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize EGL renderer\n");
            return FALSE;
    }

    if (!hwc_init_hybris_native_buffer(pScrn)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "failed to initialize libhybris native buffer EGL extension\n");
        return FALSE;
    }

    hwc->buffer = NULL;

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

            if (!xf86SetDefaultVisual(pScrn, -1))
                return FALSE;
        }
    }

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

static void hwcBlockHandler(ScreenPtr pScreen, void *timeout)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_renderer_ptr renderer = &hwc->renderer;
    PixmapPtr rootPixmap;
    int err;

    pScreen->BlockHandler = hwc->BlockHandler;
    pScreen->BlockHandler(pScreen, timeout);
    pScreen->BlockHandler = hwcBlockHandler;

    if (hwc->damage && hwc->dpmsMode == DPMSModeOn) {
        RegionPtr dirty = DamageRegion(hwc->damage);
        unsigned num_cliprects = REGION_NUM_RECTS(dirty);

        if (num_cliprects) {
            DamageEmpty(hwc->damage);
            if (hwc->glamor) {
                /* create EGL sync object so renderer thread could wait for
                 * glamor to flush commands pipeline */
                /* (just glFlush which is called in glamor's blockHandler
                   is not enough on Mali to get the buffer updated) */
                if (renderer->fence != EGL_NO_SYNC_KHR) {
                    EGLSyncKHR fence = renderer->fence;
                    renderer->fence = EGL_NO_SYNC_KHR;
                    eglDestroySyncKHR(renderer->display, fence);
                }
                renderer->fence = eglCreateSyncKHR(renderer->display,
                                                   EGL_SYNC_FENCE_KHR, NULL);
                /* make sure created sync object eventually signals */
                glFlush();
            }
            hwc_trigger_redraw(pScrn);
        }
    }
}

static Bool
CreateScreenResources(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    PixmapPtr rootPixmap;
    Bool ret;
    void *pixels = NULL;
    int err;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "CreateScreenResources virtX: %d, virtY: %d\n",pScrn->virtualX, pScrn->virtualY);

    pScreen->CreateScreenResources = hwc->CreateScreenResources;
    ret = pScreen->CreateScreenResources(pScreen);
    pScreen->CreateScreenResources = CreateScreenResources;

    rootPixmap = pScreen->GetScreenPixmap(pScreen);

#ifdef ENABLE_GLAMOR
    if (hwc->glamor) {
        pScreen->DestroyPixmap(rootPixmap);

        rootPixmap = glamor_create_pixmap(pScreen,
                                            pScreen->width,
                                            pScreen->height,
                                            pScreen->rootDepth,
                                            GLAMOR_CREATE_NO_LARGE);
        pScreen->SetScreenPixmap(rootPixmap);
    }
#endif

    err = hwc->renderer.eglHybrisCreateNativeBuffer(pScrn->virtualX, pScrn->virtualY,
                                      HYBRIS_USAGE_HW_TEXTURE |
                                      HYBRIS_USAGE_SW_READ_OFTEN|HYBRIS_USAGE_SW_WRITE_OFTEN,
                                      HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                      &hwc->stride, &hwc->buffer);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "alloc: status=%d, stride=%d\n", err, hwc->stride);

    hwc->rendererIsRunning = 1;

    if (pthread_mutex_init(&(hwc->rendererLock), NULL) ||
        pthread_mutex_init(&(hwc->dirtyLock), NULL) ||
        pthread_cond_init(&(hwc->dirtyCond), NULL) ||
        pthread_create(&(hwc->rendererThread), NULL, hwc_egl_renderer_thread, pScreen)) {
        FatalError("Error creating rendering thread\n");
    }

#ifdef ENABLE_GLAMOR
    if (hwc->glamor)
        hwc->renderer.rootTexture = glamor_get_pixmap_texture(rootPixmap);
#endif

    err = hwc->renderer.eglHybrisLockNativeBuffer(hwc->buffer,
                                    HYBRIS_USAGE_SW_READ_OFTEN|HYBRIS_USAGE_SW_WRITE_OFTEN,
                                    0, 0, hwc->stride, pScrn->virtualY, &pixels);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "gralloc lock returns %i\n", err);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "lock to vaddr %p\n", pixels);

    if (!hwc->glamor) {
        if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, hwc->stride * 4, pixels))
            FatalError("Couldn't adjust screen pixmap\n");
    }

    hwc->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE,
                                pScreen, rootPixmap);

    if (hwc->damage) {
        DamageRegister(&rootPixmap->drawable, hwc->damage);
        hwc->dirty = FALSE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to create screen damage record\n");
        return FALSE;
    }

    return ret;
}

static CARD32 hwc_update_by_timer(OsTimerPtr timer, CARD32 time, void *ptr) {
    ScreenPtr pScreen = (ScreenPtr) ptr;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    PixmapPtr rootPixmap;
    int err;

    if (hwc->dirty && hwc->dpmsMode == DPMSModeOn) {
        void *pixels = NULL;
        rootPixmap = pScreen->GetScreenPixmap(pScreen);
        hwc->renderer.eglHybrisUnlockNativeBuffer(hwc->buffer);

        hwc_egl_renderer_update(pScreen);

        err = hwc->renderer.eglHybrisLockNativeBuffer(hwc->buffer,
                        HYBRIS_USAGE_SW_READ_OFTEN|HYBRIS_USAGE_SW_WRITE_OFTEN,
                        0, 0, hwc->stride, pScrn->virtualY, &pixels);

        if (!hwc->glamor) {
            if (!pScreen->ModifyPixmapHeader(rootPixmap, -1, -1, -1, -1, -1, pixels))
                FatalError("Couldn't adjust screen pixmap\n");
        }

        hwc->dirty = FALSE;
    }

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
    void *pixels;

    /*
     * we need to get the ScrnInfoRec for this screen, so let's allocate
     * one first thing
     */
    pScrn = xf86ScreenToScrn(pScreen);
    hwc = HWCPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "ScreenInit myNum: %d, dpi: %d, width: %d\n", pScreen->myNum,pScrn->xDpi,pScrn->displayWidth);

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
    if (!ret)
        return FALSE;

    if (pScrn->depth > 8) {
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

    /* initialize XRANDR */
    xf86CrtcConfigInit(pScrn, &DUMMYCrtcConfigFuncs);
    hwc->num_screens = HWC_MAX_SCREENS;

    for (int i=0; i < hwc->num_screens; i++) {
        char szOutput[256];

		snprintf(szOutput, sizeof(szOutput), "Screen%u", i);
		//hwc_display_pre_init(pScrn, &hwc->paCrtcs[i], &hwc->paOutputs[i]);
		hwc->paCrtcs[i] = xf86CrtcCreate(pScrn, &DUMMYCrtcFuncs);
        hwc->paCrtcs[i]->driver_private = (void *)(uintptr_t)i;

        // Set up our virtual outputs.
        hwc->paOutputs[i] = xf86OutputCreate(pScrn, &DUMMYOutputFuncs, szOutput);

        xf86OutputUseScreenMonitor(hwc->paOutputs[i], FALSE);
        hwc->paOutputs[i]->possible_crtcs = 1 << i;
        hwc->paOutputs[i]->possible_clones = 0;
        hwc->paOutputs[i]->driver_private = (void *)(uintptr_t)i;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Created crtc (%p) and output %s (%p)\n",
                (void *)hwc->paCrtcs[i], szOutput,
                (void *)hwc->paOutputs[i]);
    }

    // bitmask
    hwc->connected_outputs = 1;

    xf86CrtcSetSizeRange(pScrn, 64, 64, DUMMY_MAX_WIDTH, DUMMY_MAX_HEIGHT);

    // Now create our initial CRTC/output configuration.
    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
        return (FALSE);
    }

	pScreen->SaveScreen = SaveScreen;

	/* Wrap the current CloseScreen function */
	hwc->CloseScreen = pScreen->CloseScreen;
	pScreen->CloseScreen = CloseScreen;

	hwc->CreateScreenResources = pScreen->CreateScreenResources;
	pScreen->CreateScreenResources = CreateScreenResources;

	// Initialise randr 1.2 mode-setting functions and set first mode.
    // Note that the mode won't be usable until the server has resized the
    // framebuffer to something reasonable.
    if (!xf86CrtcScreenInit(pScreen)) {
        return FALSE;
    }
    if (!xf86SetDesiredModes(pScrn)) {
        return FALSE;
    }

    /* XRANDR initialization end */

//	const char *accel_method_str;
//	accel_method_str = xf86GetOptValString(hwc->Options,
//										   OPTION_ACCEL_METHOD);
//	Bool do_glamor = (!accel_method_str ||
//				 strcmp(accel_method_str, "glamor") == 0);
//
//	if (!do_glamor) {
//		xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "glamor disabled\n");
//	}
//
//	if (!hwc_egl_renderer_init(pScrn, do_glamor)) {
//		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "failed to initialize EGL renderer\n");
//		return FALSE;
//	}
//
//	if (!hwc_init_hybris_native_buffer(pScrn)) {
//		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "failed to initialize libhybris native buffer EGL extension\n");
//		return FALSE;
//	}

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

    xf86DPMSInit(pScreen, xf86DPMSSet, 0);
    hwc->dpmsMode = DPMSModeOn;

//	if (do_glamor) {
//		try_enable_glamor(pScrn);
//	}
//	if (!hwc->glamor) {
//		try_enable_drihybris(pScrn);
//
//		if (hwc->drihybris) {
//			// Force RGBA
//			pScrn->mask.red = 0xff;
//			pScrn->mask.blue = 0xff0000;
//			pScrn->offset.red = 0;
//			pScrn->offset.blue = 16;
//
//			if (!xf86SetDefaultVisual(pScrn, -1)) {
//				xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to xf86SetDefaultVisual.\n");
//				return FALSE;
//			}
//		}
//	}

    if (hwc->glamor) {
        XF86VideoAdaptorPtr     glamor_adaptor;

        glamor_adaptor = glamor_xv_init(pScreen, 16);
        if (glamor_adaptor != NULL)
            xf86XVScreenInit(pScreen, &glamor_adaptor, 1);
        else
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "Failed to initialize XV support.\n");
    } else {
        if (hwc->drihybris) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                       "Initializing drihybris.\n");
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

#ifdef ENABLE_DRIHYBRIS
    if (hwc->drihybris) {
        drihybris_extension_init();
    }
#endif
    if (!hwc_present_screen_init(pScreen)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                    "Failed to initialize the Present extension.\n");
    }

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

    if (hwc->buffer != NULL)
    {
        hwc->renderer.eglHybrisUnlockNativeBuffer(hwc->buffer);
        hwc->renderer.eglHybrisReleaseNativeBuffer(hwc->buffer);
        hwc->buffer = NULL;
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
