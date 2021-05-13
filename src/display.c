#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <xf86.h>
#include "xf86Crtc.h"
#include <X11/Xatom.h>

#ifdef ENABLE_GLAMOR
#define GLAMOR_FOR_XORG 1
#include <glamor-hybris.h>
#endif
#ifdef ENABLE_DRIHYBRIS
#include <drihybris.h>
#endif

#include "driver.h"

Bool hwc_lights_init(ScrnInfoPtr pScrn) {
    HWCPtr hwc = HWCPTR(pScrn);
    hwc->lightsDevice = NULL;
    hw_module_t *lightsModule = NULL;
    struct light_device_t *lightsDevice = NULL;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_lights_init\n");

    /* Use 255 as default */
    hwc->screenBrightness = 255;

    if (hw_get_module(LIGHTS_HARDWARE_MODULE_ID, (const hw_module_t **) &lightsModule) != 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to get lights module\n");
        return FALSE;
    }

    if (lightsModule->methods->open(lightsModule, LIGHT_ID_BACKLIGHT, (hw_device_t * *) & lightsDevice) != 0) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create lights device\n");
        return FALSE;
    }

    hwc->lightsDevice = lightsDevice;
    return TRUE;
}

void hwc_lights_close(ScrnInfoPtr pScrn) {
    HWCPtr hwc = HWCPTR(pScrn);
    hw_module_t *lightsModule = NULL;

    assert(hwc->lightsDevice);

    hwc->lightsDevice->common.close((hw_device_t *) hwc->lightsDevice);
}

Bool hwc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y);

Bool DUMMYAdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height) {
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    void *pixels = NULL;
    hwc_display_ptr hwc_display = &hwc->primary_display;
    uint64_t cbLine = (width * xf86GetBppFromDepth(pScrn, pScrn->depth) / 8 + 3) & ~3;
    int displayWidth = cbLine * 8 / xf86GetBppFromDepth(pScrn, pScrn->depth);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap in X: %d, Y: %d, dW: %d\n", pScrn->virtualX,
               pScrn->virtualY, pScrn->displayWidth);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap target X: %d, Y: %d, dW: %d\n", width,
               height, displayWidth);

    if (width == pScrn->virtualX && height == pScrn->virtualY && displayWidth == pScrn->displayWidth) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap no change\n");
        return TRUE;
    }

    if (!pPixmap) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to get the screen pixmap.\n");
        return FALSE;
    }

    pthread_mutex_lock(&(hwc->dirtyLock));

    if (hwc_display->damage) {
        DamageUnregister(hwc_display->damage);
        DamageDestroy(hwc_display->damage);
        hwc_display->damage = NULL;
    }

    if (hwc_display->buffer != NULL) {
        hwc->egl_proc.eglHybrisUnlockNativeBuffer(hwc_display->buffer);
        hwc->egl_proc.eglHybrisReleaseNativeBuffer(hwc_display->buffer);
        hwc_display->buffer = NULL;
    }

    pScreen->ModifyPixmapHeader(pPixmap, width, height,
                                pScrn->depth, xf86GetBppFromDepth(pScrn, pScrn->depth), cbLine,
                                pPixmap->devPrivate.ptr);
    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = displayWidth;

    int err = hwc->egl_proc.eglHybrisCreateNativeBuffer(width, height,
                                                        HYBRIS_USAGE_HW_TEXTURE |
                                                        HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                        HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                                        &hwc_display->stride, &hwc_display->buffer);

    if (hwc->glamor) {
#ifdef ENABLE_GLAMOR
        hwc_display->hwc_renderer.rootTexture = glamor_get_pixmap_texture(pPixmap);
#endif
    }

    err = hwc->egl_proc.eglHybrisLockNativeBuffer(hwc_display->buffer,
                                                  HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                  0, 0, hwc_display->stride, height, &pixels);

    hwc_display->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE, pScreen, pPixmap);

    if (hwc_display->damage) {
        DamageRegister(&pPixmap->drawable, hwc_display->damage);
        hwc_display->dirty = FALSE;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
    } else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create screen damage record\n");
        return FALSE;
    }

    if (!hwc_egl_renderer_tidy(pScrn, &hwc->primary_display)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "failed to tidyup primary EGL renderer\n");
        return FALSE;
    }

    const char *accel_method_str = xf86GetOptValString(hwc->Options, OPTION_ACCEL_METHOD);
    Bool do_glamor = (!accel_method_str ||
                 strcmp(accel_method_str, "glamor") == 0);

    if (!hwc_egl_renderer_init(pScrn, &hwc->primary_display, do_glamor)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "failed to initialize primary EGL renderer\n");
        return FALSE;
    }

//    pScrn->virtualX = pScrn->displayWidth = xf86ModeWidth(pScrn->modes, hwc->primary_display.rotation);
//    pScrn->virtualY = xf86ModeHeight(pScrn->modes, hwc->primary_display.rotation);
//    hwc_set_mode_major(hwc->paCrtcs[0], pScrn->modes, HWC_ROTATE_CCW, pScrn->virtualX, pScrn->virtualY);

//    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
//    for (int i = 0; i < xf86_config->num_crtc; i++) {
//        xf86CrtcPtr crtc = xf86_config->crtc[i];
//
//        if (!crtc->enabled)
//            continue;
//
//        hwc_set_mode_major(crtc, &crtc->mode, crtc->rotation, crtc->x, crtc->y);
//    }

    pthread_mutex_unlock(&(hwc->dirtyLock));

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "DUMMYAdjustScreenPixmap out X: %d, Y: %d, dW: %d\n", pScrn->virtualX,
               pScrn->virtualY, pScrn->displayWidth);

    return TRUE;
}

static Bool
hwc_xf86crtc_resize(ScrnInfoPtr pScrn, int cw, int ch) {
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
hwc_crtc_dpms(xf86CrtcPtr crtc, int mode) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_crtc_dpms mode: %d, index: %d\n", mode, index);

    //handled in hwc_output_dpms
}

static Bool
dummy_crtc_lock(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_lock index: %d\n", index);
    //other folk wait for GPU idle here
    return FALSE;
}

static Bool
dummy_crtc_mode_fixup(xf86CrtcPtr crtc, DisplayModePtr mode,
                      DisplayModePtr adjusted_mode) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_mode_fixup mode: %p, adjusted_mode: %p, index: %d\n", mode,
               adjusted_mode, index);
    return TRUE;
}

static void
dummy_crtc_prepare(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_prepare index:%d\n", index);
}

static void
dummy_crtc_commit(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_commit index:%d\n", index);
}

static void
dummy_crtc_destroy(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_destroy index: %d\n", index);
}

static void
dummy_crtc_gamma_set(xf86CrtcPtr crtc, CARD16 *red, CARD16 *green, CARD16 *blue, int size) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_gamma_set size: %d, index: %d\n", size, index);

//	drmModeCrtcSetGamma(fd, crtc_id, size, red, green, blue);
}

static void *
dummy_crtc_shadow_allocate(xf86CrtcPtr crtc, int width, int height) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_shadow_allocate width: %d, height: %d, index: %d\n", width,
               height, index);
    return NULL;
}

PixmapPtr
dummy_crtc_shadow_create(xf86CrtcPtr crtc, void *data, int width, int height) {
    PixmapPtr crtcPixmap;
    void *pixels = NULL;
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_shadow_create width: %d, height: %d, index: %d\n", width,
               height, index);

    crtcPixmap = crtc->scrn->pScreen->GetScreenPixmap(crtc->scrn->pScreen);
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "got pixmap: %p, index: %d\n", crtcPixmap, index);

#ifdef ENABLE_GLAMOR
    if (hwc->glamor) {
        if (crtcPixmap) {
            crtc->scrn->pScreen->DestroyPixmap(crtcPixmap);
        }
        crtcPixmap = glamor_create_pixmap(crtc->scrn->pScreen,
                                            width,
                                            height,
                                            crtc->scrn->pScreen->rootDepth,//PIXMAN_FORMAT_DEPTH(HYBRIS_PIXEL_FORMAT_RGBA_8888),
                                            GLAMOR_CREATE_NO_LARGE);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "created glamor pixmap, index: %d\n", index);
        crtc->scrn->pScreen->SetScreenPixmap(crtcPixmap);
    }
#endif

    int err = hwc->egl_proc.eglHybrisCreateNativeBuffer(width, height,
                                                        HYBRIS_USAGE_HW_TEXTURE |
                                                        HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                        HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                                        &hwc_display->stride, &hwc_display->buffer);

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "alloc: status=%d, stride=%d\n", err, hwc_display->stride);

    if (hwc->glamor) {
#ifdef ENABLE_GLAMOR
        hwc_display->hwc_renderer.rootTexture = glamor_get_pixmap_texture(crtcPixmap);
#endif
    }

    err = hwc->egl_proc.eglHybrisLockNativeBuffer(hwc_display->buffer,
                                                  HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                  0, 0, hwc_display->stride, height, &pixels);

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "gralloc lock returns %i, lock to vaddr %p, index: %d\n", err, pixels,
               index);

    if (!hwc->glamor) {
        if (!crtc->scrn->pScreen->ModifyPixmapHeader(crtcPixmap, -1, -1, -1, -1, hwc_display->stride * 4, pixels)) {
            FatalError("Couldn't adjust screen pixmap, index: %d\n", index);
        }
    }

    hwc_display->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE, crtc->scrn->pScreen, crtcPixmap);

    if (hwc_display->damage) {
        DamageRegister(&crtcPixmap->drawable, hwc_display->damage);
        hwc_display->dirty = FALSE;
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "Damage tracking initialized, index: %d\n", index);
    } else {
        xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR, "Failed to create screen damage record, index: %d\n", index);
        return FALSE;
    }

    return crtcPixmap;
}

void
dummy_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr pPixmap, void *data) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "dummy_crtc_shadow_destroy pPixmap: %p, data: %p, index: %d\n", pPixmap,
               data, index);

    if (pPixmap) {
        pPixmap->drawable.pScreen->DestroyPixmap(pPixmap);
    }

    if (hwc_display->damage) {
        DamageUnregister(hwc_display->damage);
        DamageDestroy(hwc_display->damage);
        hwc_display->damage = NULL;
    }

    if (hwc_display->buffer != NULL) {
        hwc->egl_proc.eglHybrisUnlockNativeBuffer(hwc_display->buffer);
        hwc->egl_proc.eglHybrisReleaseNativeBuffer(hwc_display->buffer);
        hwc_display->buffer = NULL;
    }

}

static void
dummy_crtc_mode_set(xf86CrtcPtr crtc, DisplayModePtr mode,
                    DisplayModePtr adjusted_mode, int x, int y) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "dummy_crtc_mode_set mode: %p, adjusted_mode: %p, x: %d, y: %d, index: %d\n", mode, adjusted_mode, x, y,
               index);
}

Bool
hwc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
//    x = xf86ModeWidth(mode, rotation);
//    y = xf86ModeHeight(mode, rotation);
//    crtc->desiredMode = *mode;
//    crtc->desiredX = x;
//    crtc->desiredY = y;
//    crtc->desiredRotation = rotation;
//    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_mode_major before x: %d, y: %d, rotation: %d\n", crtc->x, crtc->y, crtc->rotation);
//    crtc->mode = *mode;
//    crtc->x = x;
//    crtc->y = y;
//    crtc->rotation = rotation;
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_mode_major as setting x: %d, y: %d, rotation: %d index: %d\n", x, y, rotation, index);

    if (index == 0) {
        char buf[100];
        hwc->primary_display.rotation = rotation;
        snprintf(buf, sizeof buf, "echo %d > /sys/devices/platform/touch/screen_rotation", rotation);
        int ret = system(buf);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_mode_major updated touch %d\n", ret);
    } else {
        hwc->external_display.rotation = rotation;
    }

    return TRUE;
}

static void
hwc_set_cursor_colors(xf86CrtcPtr crtc, int bg, int fg) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_cursor_colors index: %d\n", index);

}

static void
hwc_set_cursor_position(xf86CrtcPtr crtc, int x, int y) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_set_cursor_position index: %d, x: %d, y: %d\n", index, x, y);

    hwc_display->cursorX = x;
    hwc_display->cursorY = y;
    hwc_trigger_redraw(crtc->scrn, hwc_display);
}

/*
 * The load_cursor_argb_check driver hook.
 *
 * Sets the hardware cursor by uploading it to texture.
 * On failure, returns FALSE indicating that the X server should fall
 * back to software cursors.
 */
static Bool
hwc_load_cursor_argb_check(xf86CrtcPtr crtc, CARD32 *image) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_load_cursor_argb_check index: %d\n", index);

    glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, hwc->cursorWidth, hwc->cursorHeight,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, image);

    hwc_trigger_redraw(crtc->scrn, hwc_display);
    return TRUE;
}

static void
hwc_hide_cursor(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    hwc_display->cursorShown = FALSE;

    hwc_trigger_redraw(crtc->scrn, hwc_display);
}

static void
hwc_show_cursor(xf86CrtcPtr crtc) {
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    hwc_display->cursorShown = TRUE;

    hwc_trigger_redraw(crtc->scrn, hwc_display);
}

static const xf86CrtcFuncsRec hwcomposer_crtc_funcs = {
        .dpms = hwc_crtc_dpms,
//	.save = NULL, /* These two are never called by the server. */
//	.restore = NULL,
//	.lock = dummy_crtc_lock,
//	.unlock = NULL, /* This will not be invoked if lock returns FALSE. */
//	.mode_fixup = dummy_crtc_mode_fixup,
//	.prepare = dummy_crtc_prepare,
//	.mode_set = dummy_crtc_mode_set,
//	.commit = dummy_crtc_commit,
        .gamma_set = dummy_crtc_gamma_set,
        .shadow_allocate = dummy_crtc_shadow_allocate,
        .shadow_create = dummy_crtc_shadow_create,
        .shadow_destroy = dummy_crtc_shadow_destroy,
//	.load_cursor_argb = NULL,
        .destroy = dummy_crtc_destroy,
        .set_mode_major = hwc_set_mode_major,
        .set_cursor_colors = hwc_set_cursor_colors,
        .set_cursor_position = hwc_set_cursor_position,
        .show_cursor = hwc_show_cursor,
        .hide_cursor = hwc_hide_cursor,
        .load_cursor_argb_check = hwc_load_cursor_argb_check
};


static void
dummy_output_prepare(xf86OutputPtr output) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_prepare index: %d\n", index);
}

static void
dummy_output_commit(xf86OutputPtr output) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_commit index: %d\n", index);
}

static void
dummy_output_destroy(xf86OutputPtr output) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_destroy index: %d\n", index);
}

xf86CrtcPtr
dummy_output_get_crtc(xf86OutputPtr output) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_get_crtc index: %d\n", index);
}

static void
hwc_output_dpms(xf86OutputPtr output, int mode) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_dpms mode: %d, index: %d\n", mode, index);

    ScrnInfoPtr pScrn;
    pScrn = output->scrn;

    hwc_display->dpmsMode = mode;

    if (index == 1 && !(hwc->connected_outputs & (1 << index))) {
        return;
    }

    if (mode != DPMSModeOn) {
        // Wait for the renderer thread to finish to avoid causing locks
        pthread_mutex_lock(&(hwc->dirtyLock));
        hwc_display->dirty = FALSE;
        pthread_mutex_unlock(&(hwc->dirtyLock));
    }

    pthread_mutex_lock(&(hwc->rendererLock));
    hwc_set_power_mode(pScrn, index, (mode == DPMSModeOn) ? 1 : 0);
    pthread_mutex_unlock(&(hwc->rendererLock));

    hwc_toggle_screen_brightness(pScrn);

    if (mode == DPMSModeOn)
        // Force redraw after unblank
        hwc_trigger_redraw(pScrn, hwc_display);
}

static int
dummy_output_mode_valid(xf86OutputPtr output, DisplayModePtr mode) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_valid mode: %p, index: %d\n", mode, index);
    return MODE_OK;
}

static Bool
dummy_output_mode_fixup(xf86OutputPtr output, DisplayModePtr mode,
                        DisplayModePtr adjusted_mode) {
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_fixup mode: %p, adjusted_mode: %p, index: %d\n",
               mode, adjusted_mode, index);
    return TRUE;
}

static void
dummy_output_mode_set(xf86OutputPtr output, DisplayModePtr mode,
                      DisplayModePtr adjusted_mode) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_set mode: %p, adjusted_mode: %p, index: %d\n", mode,
               adjusted_mode, index);

    /* set to connected at first mode set */
    hwc->connected_outputs |= 1 << index;
}

/* The first virtual monitor is always connected. Others only after setting its
 * mode */
static xf86OutputStatus
hwc_output_detect(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_detect index: %d\n", index);

    if (hwc->connected_outputs & (1 << index))
        return XF86OutputStatusConnected;
    else
        return XF86OutputStatusDisconnected;
}

static DisplayModePtr
hwc_output_get_modes(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_get_modes - pW: %d, pH: %d, eW: %d, eH:%d index: %d\n", hwc->primary_display.width, hwc->primary_display.height, hwc->external_display.width, hwc->external_display.height, index);

    if (index == 0) {
        return xf86CVTMode(hwc->primary_display.width, hwc->primary_display.height, 60, 0, 0);
    } else {
        return xf86CVTMode(hwc->external_display.width, hwc->external_display.height, 60, 0, 0);
    }
}

void dummy_output_register_prop(xf86OutputPtr output, Atom prop, uint64_t value) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_register_prop %d\n", index);

//    INT32 dims_range[2] = {0, 65535};
//    int err;

//    err = RRConfigureOutputProperty(output->randr_output, prop, FALSE,
//                                    TRUE, FALSE, 2, dims_range);
//    if (err != 0)
//        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
//                   "RRConfigureOutputProperty error, %d\n", err);
//
//    err = RRChangeOutputProperty(output->randr_output, prop, XA_INTEGER,
//                                 32, PropModeReplace, 1, &value, FALSE, FALSE);
//    if (err != 0)
//        xf86DrvMsg(output->scrn->scrnIndex, X_ERROR,
//                   "RRChangeOutputProperty error, %d\n", err);
}

//Atom width_mm_atom = 0;
//#define WIDTH_MM_NAME  "WIDTH_MM"
//Atom height_mm_atom = 0;
//#define HEIGHT_MM_NAME "HEIGHT_MM"

void dummy_output_create_resources(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_create_resources %d\n", index);

//    if (!ValidAtom(width_mm_atom))
//        width_mm_atom = MakeAtom(WIDTH_MM_NAME, strlen(WIDTH_MM_NAME), 1);
//    if (!ValidAtom(height_mm_atom))
//        height_mm_atom = MakeAtom(HEIGHT_MM_NAME, strlen(HEIGHT_MM_NAME), 1);
//
//    dummy_output_register_prop(output, width_mm_atom, 0);
//    dummy_output_register_prop(output, height_mm_atom, 0);
}

static Bool dummy_output_set_property(xf86OutputPtr output, Atom property,
                                      RRPropertyValuePtr value) {
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_set_property prop: %d, valT: %d\n", property,
               value->type);

//    if (property == width_mm_atom || property == height_mm_atom) {
//        INT32 val;
//
//        if (value->type != XA_INTEGER || value->format != 32 ||
//            value->size != 1) {
//            return FALSE;
//        }
//
//        val = *(INT32 *) value->data;
//        if (property == width_mm_atom)
//            output->mm_width = val;
//        else if (property == height_mm_atom)
//            output->mm_height = val;
//        return TRUE;
//    }
//    return TRUE;
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
#ifdef RANDR_GET_CRTC_INTERFACE
        .get_crtc = dummy_output_get_crtc,
#endif
        .destroy = dummy_output_destroy
};

void
hwc_trigger_redraw(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display) {
    HWCPtr hwc = HWCPTR(pScrn);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_trigger_redraw index: %p\n", hwc_display);

    pthread_mutex_lock(&(hwc->dirtyLock));
    hwc_display->dirty = TRUE;
    pthread_cond_signal(&(hwc->dirtyCond));
    pthread_mutex_unlock(&(hwc->dirtyLock));
}

Bool
hwc_display_pre_init(ScrnInfoPtr pScrn) {
    ScreenPtr pScreen = pScrn->pScreen;
    HWCPtr hwc = HWCPTR(pScrn);

    /* initialize XRANDR */
    xf86CrtcConfigInit(pScrn, &hwc_xf86crtc_config_funcs);
    xf86CrtcSetSizeRange(pScrn, 8, 8, SHRT_MAX, SHRT_MAX);

    hwc->num_screens = HWC_MAX_SCREENS;

    for (int i = 0; i < hwc->num_screens; i++) {
        char szOutput[256];

        hwc->paCrtcs[i] = xf86CrtcCreate(pScrn, &hwcomposer_crtc_funcs);
        hwc->paCrtcs[i]->driver_private = (void *) (uintptr_t) i;

        snprintf(szOutput, sizeof(szOutput), "Screen%u", i);
        hwc->paOutputs[i] = xf86OutputCreate(pScrn, &hwcomposer_output_funcs, szOutput);

        xf86OutputUseScreenMonitor(hwc->paOutputs[i], (i == 0));
        hwc->paOutputs[i]->possible_crtcs = 1 << i;
        hwc->paOutputs[i]->possible_clones = 0;
        hwc->paOutputs[i]->driver_private = (void *) (uintptr_t) i;

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Created crtc (%p) and output %s (%p)\n",
                   (void *) hwc->paCrtcs[i], szOutput,
                   (void *) hwc->paOutputs[i]);

        const char *s;
        if (i == 0 && (s = xf86GetOptValString(hwc->Options, OPTION_ROTATE))) {
            if (!xf86NameCmp(s, "right")) {
                hwc->primary_display.rotation = HWC_ROTATE_CW;
                hwc->paCrtcs[i]->desiredRotation = HWC_ROTATE_CW;
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen clockwise\n");
            } else if (!xf86NameCmp(s, "inverted")) {
                hwc->primary_display.rotation = HWC_ROTATE_UD;
                hwc->paCrtcs[i]->desiredRotation = HWC_ROTATE_UD;
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen upside-down\n");
            } else if (!xf86NameCmp(s, "left")) {
                hwc->primary_display.rotation = HWC_ROTATE_CCW;
                hwc->paCrtcs[i]->desiredRotation = HWC_ROTATE_CCW;
//                hwc->paOutputs[i]->initial_rotation = HWC_ROTATE_CCW;
//                hwc->paOutputs[i]->initial_x = hwc->primary_display.height;
//                hwc->paOutputs[i]->initial_y = hwc->primary_display.width;
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG, "rotating screen counter-clockwise\n");
            } else {
                xf86DrvMsg(pScrn->scrnIndex, X_CONFIG,
                           "\"%s\" is not a valid value for Option \"Rotate\"\n", s);
                xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                           "valid options are \"right\", \"inverted\", \"left\"\n");
            }
        }
    }

    // bitmask
    hwc->connected_outputs = 0b01; //TODO need to actually do a detection?

    // Pick rotated HWComposer screen resolution
//    pScrn->virtualX = hwc->primary_display.width;  //startsmall?
//    pScrn->virtualY = hwc->primary_display.height;
//    pScrn->initial_rotation = HWC_ROTATE_CCW;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init primary picked\n");
//    pScrn->displayWidth = pScrn->virtualX;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init dx: %d, w: %d, h: %d, outputs: %d\n",
               pScrn->display->virtualX, pScrn->virtualX, pScrn->virtualY, hwc->connected_outputs);

    // Construct a mode with the screen's initial dimensions
//    hwc->modes = xf86CVTMode(hwc->primary_display.width, hwc->primary_display.height, 60, 0, 0);

    xf86ProviderSetup(pScrn, NULL, "hwcomposer");

    // Now create our initial CRTC/output configuration.
    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
        return (FALSE);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init dx: %d, w: %d, h: %d, outputs: %d\n",
               pScrn->display->virtualX, pScrn->virtualX, pScrn->virtualY, hwc->connected_outputs);

//    pScrn->virtualX = xf86ModeWidth(pScrn->modes, HWC_ROTATE_CCW);
//    pScrn->virtualY = xf86ModeHeight(pScrn->modes, HWC_ROTATE_CCW);
//    hwc_set_mode_major(hwc->paCrtcs[0], pScrn->modes, HWC_ROTATE_CCW, pScrn->virtualX, pScrn->virtualY);

    pScrn->currentMode = pScrn->modes;

    return TRUE;
}
