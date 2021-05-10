#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <xf86.h>
#include "xf86Crtc.h"
#include <X11/Xatom.h>

#include "driver.h"

Bool hwc_lights_init(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	hwc->lightsDevice = NULL;
	hw_module_t *lightsModule = NULL;
	struct light_device_t *lightsDevice = NULL;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_lights_init\n");

	/* Use 255 as default */
	hwc->screenBrightness = 255;

	if (hw_get_module(LIGHTS_HARDWARE_MODULE_ID, (const hw_module_t **)&lightsModule) != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to get lights module\n");
		return FALSE;
	}

	if (lightsModule->methods->open(lightsModule, LIGHT_ID_BACKLIGHT, (hw_device_t **)&lightsDevice) != 0) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create lights device\n");
		return FALSE;
	}

	hwc->lightsDevice = lightsDevice;
	return TRUE;
}

void hwc_lights_close(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	hw_module_t *lightsModule = NULL;

	assert(hwc->lightsDevice);

	hwc->lightsDevice->common.close((hw_device_t*)hwc->lightsDevice);
}

Bool DUMMYAdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height)
{
	ScreenPtr pScreen = pScrn->pScreen;
	PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
	HWCPtr hwc = HWCPTR(pScrn);
	uint64_t cbLine = (width * xf86GetBppFromDepth(pScrn, pScrn->depth) / 8 + 3) & ~3;
	int displayWidth = cbLine * 8 / xf86GetBppFromDepth(pScrn, pScrn->depth);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap in X: %d, Y: %d, dW: %d\n", pScrn->virtualX, pScrn->virtualY, displayWidth);

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

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap out X: %d, Y: %d, dW: %d\n", pScrn->virtualX, pScrn->virtualY, pScrn->displayWidth);

	return TRUE;
}

static Bool
hwc_xf86crtc_resize(ScrnInfoPtr pScrn, int cw, int ch)
{
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_xf86crtc_resize cw: %d, ch: %d\n", cw, ch);
	if (!pScrn->vtSema) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
				   "We do not own the active VT, exiting.\n");
		return TRUE;
	}
	return DUMMYAdjustScreenPixmap(pScrn, cw, ch);
}

static const xf86CrtcConfigFuncsRec hwc_xf86crtc_config_funcs = {
		.resize = hwc_xf86crtc_resize
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
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_mode_fixup mode: %p, adjusted_mode: %p, index: %d\n", mode, adjusted_mode, index);
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
//	crtc->rotation = rotation;
	int index = (int64_t)crtc->driver_private;
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_mode_major index: %d, *ignored* rotation: %d\n", index, rotation);

//	if (index == 0) {
//		hwc->primary_display.rotation = rotation;
//	} else {
//		hwc->external_display.rotation = rotation;
//	}

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
	int index = (int64_t)crtc->driver_private;
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_cursor_position index: %d, x: %d, y: %d\n", index, x, y);
	hwc->cursorX = x;
	hwc->cursorY = y;
	hwc_trigger_redraw(crtc->scrn, index);
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
	int index = (int64_t)crtc->driver_private;
	xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_load_cursor_argb_check index: %d\n", index);

	glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hwc->cursorWidth, hwc->cursorHeight,
				 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

	hwc_trigger_redraw(crtc->scrn, index);
	return TRUE;
}

static void
hwc_hide_cursor(xf86CrtcPtr crtc)
{
	HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
	hwc->cursorShown = FALSE;
	hwc_trigger_redraw(crtc->scrn, index);
}

static void
hwc_show_cursor(xf86CrtcPtr crtc)
{
	HWCPtr hwc = HWCPTR(crtc->scrn);
	int index = (int64_t)crtc->driver_private;
	hwc->cursorShown = TRUE;
	hwc_trigger_redraw(crtc->scrn, index);
}

static const xf86CrtcFuncsRec hwcomposer_crtc_funcs = {
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
		hwc->primary_display.dpmsMode = mode;
	} else {
		hwc->external_display.dpmsMode = mode;
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
		hwc_trigger_redraw(pScrn, index);
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

Atom width_mm_atom = 0;
#define WIDTH_MM_NAME  "WIDTH_MM"
Atom height_mm_atom = 0;
#define HEIGHT_MM_NAME "HEIGHT_MM"

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

static const xf86OutputFuncsRec hwcomposer_output_funcs = {
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

void
hwc_trigger_redraw(ScrnInfoPtr pScrn, int disp)
{
    HWCPtr hwc = HWCPTR(pScrn);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_trigger_redraw %d\n", disp);

    pthread_mutex_lock(&(hwc->dirtyLock));
    hwc->dirty = TRUE;
    pthread_cond_signal(&(hwc->dirtyCond));
    pthread_mutex_unlock(&(hwc->dirtyLock));
}

Bool
hwc_display_pre_init(ScrnInfoPtr pScrn, xf86CrtcPtr *crtc, xf86OutputPtr *output)
{
    HWCPtr hwc = HWCPTR(pScrn);

    // Pick up size from the "Display" subsection if it exists
    if (pScrn->display->virtualX) {
        pScrn->virtualX = pScrn->display->virtualX;
        pScrn->virtualY = pScrn->display->virtualY;
    } else {
        // Pick rotated HWComposer screen resolution
        pScrn->virtualX = hwc->primary_display.height;
        pScrn->virtualY = hwc->primary_display.width;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init primary picked\n");
    }
    pScrn->displayWidth = pScrn->virtualX;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init dx: %d, w: %d, h: %d, outputs: %d\n", pScrn->display->virtualX, pScrn->virtualX, pScrn->virtualY, hwc->connected_outputs);

    // Construct a mode with the screen's initial dimensions
    hwc->modes = xf86CVTMode(pScrn->virtualX, pScrn->virtualY, 60, 0, 0);

	/* initialize XRANDR */
	xf86CrtcConfigInit(pScrn, &hwc_xf86crtc_config_funcs);
	xf86CrtcSetSizeRange(pScrn, 8, 8, SHRT_MAX, SHRT_MAX);

	hwc->num_screens = HWC_MAX_SCREENS;

	for (int i=0; i < hwc->num_screens; i++) {
		char szOutput[256];

		hwc->paCrtcs[i] = xf86CrtcCreate(pScrn, &hwcomposer_crtc_funcs);
		hwc->paCrtcs[i]->driver_private = (void *)(uintptr_t)i;

		// Set up our virtual outputs.
		snprintf(szOutput, sizeof(szOutput), "Screen%u", i);
		hwc->paOutputs[i] = xf86OutputCreate(pScrn, &hwcomposer_output_funcs, szOutput);

		xf86OutputUseScreenMonitor(hwc->paOutputs[i], (i == 0));
		hwc->paOutputs[i]->possible_crtcs = 1 << i;
		hwc->paOutputs[i]->possible_clones = 0;
		hwc->paOutputs[i]->driver_private = (void *)(uintptr_t)i;
		xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Created crtc (%p) and output %s (%p)\n",
				   (void *)hwc->paCrtcs[i], szOutput,
				   (void *)hwc->paOutputs[i]);

		//		hwc->rotation = HWC_ROTATE_NORMAL;
		const char *s;
		if (i==0 && (s = xf86GetOptValString(hwc->Options, OPTION_ROTATE)))
		{
			if(!xf86NameCmp(s, "CW")) {
				hwc->primary_display.rotation = HWC_ROTATE_CW;
				xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen clockwise\n");
			}
			else if(!xf86NameCmp(s, "UD")) {
				hwc->primary_display.rotation = HWC_ROTATE_UD;
				xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen upside-down\n");
			}
			else if(!xf86NameCmp(s, "CCW")) {
				hwc->primary_display.rotation = HWC_ROTATE_CCW;
				xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen counter-clockwise\n");
			}
			else {
				xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
						   "\"%s\" is not a valid value for Option \"Rotate\"\n", s);
				xf86DrvMsg(pScrn->scrnIndex, X_INFO,
						   "valid options are \"CW\", \"UD\", \"CCW\"\n");
			}
		}
	}

	// bitmask
	hwc->connected_outputs = 0b01;

	xf86ProviderSetup(pScrn, NULL, "hwcomposer");

	// Now create our initial CRTC/output configuration.
	if (!xf86InitialConfiguration(pScrn, TRUE)) {
		xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
		return (FALSE);
	}

//	*output = xf86OutputCreate(pScrn, &hwcomposer_output_funcs, "hwcomposer");
//	(*output)->possible_crtcs = 0x7f;

//	*crtc = xf86CrtcCreate(pScrn, &hwcomposer_crtc_funcs);

//	xf86ProviderSetup(pScrn, NULL, "hwcomposer");
//
//	xf86InitialConfiguration(pScrn, TRUE);

	pScrn->currentMode = pScrn->modes;
//	(*crtc)->funcs->set_mode_major(*crtc, pScrn->currentMode, RR_Rotate_0, 0, 0);

    return TRUE;
}
