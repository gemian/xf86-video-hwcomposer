#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include <xf86.h>
#include "xf86Crtc.h"
#include <X11/Xatom.h>
#include <sys/stat.h>
#include <fcntl.h>

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

    if (hwc->lightsDevice == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_lights_close lightsDevice: %p\n", hwc->lightsDevice);
    }
    assert(hwc->lightsDevice);

    hwc->lightsDevice->common.close((hw_device_t *) hwc->lightsDevice);
}

Bool hwc_set_mode_major(xf86CrtcPtr crtc, DisplayModePtr mode, Rotation rotation, int x, int y);

Bool AdjustScreenPixmap(ScrnInfoPtr pScrn, int width, int height) {
    ScreenPtr pScreen = pScrn->pScreen;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
//    void *pixels = NULL;
////    hwc_display_ptr hwc_display = &hwc->primary_display;
    uint64_t cbLine = (width * xf86GetBppFromDepth(pScrn, pScrn->depth) / 8 + 3) & ~3;
    int displayWidth = cbLine * 8 / xf86GetBppFromDepth(pScrn, pScrn->depth);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AdjustScreenPixmap in virtualX: %d, virtualY: %d, dW: %d\n",
               pScrn->virtualX, pScrn->virtualY, pScrn->displayWidth);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AdjustScreenPixmap target W: %d, H: %d, dW: %d\n", width,
               height, displayWidth);
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
//               "AdjustScreenPixmap screen w: %d, h: %d, cBx1: %d, cBy1: %d, cBx2: %d, cBy2: %d\n",
//               pScreen->width, pScreen->height, hwc_display->pCrtc->bounds.x1, hwc_display->pCrtc->bounds.y1,
//               hwc_display->pCrtc->bounds.x2, hwc_display->pCrtc->bounds.y2);

    if (width == pScrn->virtualX && height == pScrn->virtualY && displayWidth == pScrn->displayWidth) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AdjustScreenPixmap no change\n");
        return TRUE;
    }
//
//    if (hwc->damage) {
//        DamageUnregister(hwc->damage);
//        DamageDestroy(hwc->damage);
//        hwc->damage = NULL;
//        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking tidied up\n");
//    }
//
    if (hwc->glamor) {
#ifdef ENABLE_GLAMOR
        if (pPixmap) {
            pScreen->DestroyPixmap(pPixmap);
        }
        pPixmap = glamor_create_pixmap(pScreen,
                                       width,
                                       height,
                                       pScrn->depth,
                                       GLAMOR_CREATE_NO_LARGE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Created glamor screen pixmap: %p\n", pPixmap);
        pScreen->SetScreenPixmap(pPixmap);
#endif
    } else {
        if (pPixmap) {
            pScreen->DestroyPixmap(pPixmap);
        }
        pPixmap = (*pScreen->CreatePixmap)(pScreen, width, height, pScrn->depth, 0);
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Created screen pixmap: %p\n", pPixmap);
        pScreen->SetScreenPixmap(pPixmap);
    }

    if (!pPixmap) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create a new screen pixmap.\n");
        return FALSE;
    }

//    if (hwc->buffer != NULL) {
//        hwc->egl_proc.eglHybrisUnlockNativeBuffer(hwc->buffer);
//        hwc->egl_proc.eglHybrisReleaseNativeBuffer(hwc->buffer);
//        hwc->buffer = NULL;
//    }
//
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Released buffer\n");
//
//    pScreen->ModifyPixmapHeader(pPixmap, width, height,
//                                pScrn->depth, xf86GetBppFromDepth(pScrn, pScrn->depth), cbLine,
//                                pPixmap->devPrivate.ptr);
//    pScrn->virtualX = width;
//    pScrn->virtualY = height;
//    pScrn->displayWidth = displayWidth;
//
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Modified Pixmap Header\n");
//
//    int err = hwc->egl_proc.eglHybrisCreateNativeBuffer(width, height,
//                                                        HYBRIS_USAGE_HW_TEXTURE |
//                                                        HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
//                                                        HYBRIS_PIXEL_FORMAT_RGBA_8888,
//                                                        &hwc->stride, &hwc->buffer);
//
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "eglHybrisCreateNativeBuffer\n");
//
//    if (hwc->glamor) {
//#ifdef ENABLE_GLAMOR
//        hwc->rootTexture = glamor_get_pixmap_texture(pPixmap);
//#endif
//    }
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "glamor_get_pixmap_texture\n");
//
//    hwc->height = height;
//    err = hwc->egl_proc.eglHybrisLockNativeBuffer(hwc->buffer,
//                                                  HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
//                                                  0, 0, hwc->stride, hwc->height, &pixels);
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "eglHybrisLockNativeBuffer\n");
//
//    hwc->damage = DamageCreate(NULL, NULL, DamageReportNone, TRUE, pScreen, pPixmap);
//    if (!hwc->damage) {
//        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Failed to create screen damage record\n");
//        return FALSE;
//    }
//    DamageRegister(&pPixmap->drawable, hwc->damage);
//    hwc->dirty = FALSE;
//    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Damage tracking initialized\n");
//
    hwc->wasRotated = TRUE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AdjustScreenPixmap out virtualX: %d, virtualY: %d, displayWidth: %d\n", pScrn->virtualX,
               pScrn->virtualY, pScrn->displayWidth);

    return TRUE;
}

static Bool
hwc_xf86crtc_resize(ScrnInfoPtr pScrn, int cw, int ch) {
    HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_xf86crtc_resize cw: %d, ch: %d, xDpi: %d, yDpi: %d\n", cw, ch,
               pScrn->xDpi, pScrn->yDpi);
    if (!pScrn->vtSema) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "We do not own the active VT, exiting.\n");
        return TRUE;
    }

    pthread_mutex_lock(&(hwc->rendererLock));
    pthread_mutex_lock(&(hwc->dirtyLock));

    Bool ret = AdjustScreenPixmap(pScrn, cw, ch);

    pthread_mutex_unlock(&(hwc->dirtyLock));
    pthread_mutex_unlock(&(hwc->rendererLock));
    hwc_trigger_redraw(pScrn);
    return ret;
//    return TRUE;
}

static const xf86CrtcConfigFuncsRec hwc_xf86crtc_config_funcs = {
        .resize = hwc_xf86crtc_resize,
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
    if (index == HWC_DISPLAY_PRIMARY) {
        return &hwc->primary_display;
    } else {
        return &hwc->external_display;
    }
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

    return get_crtc_pixmap(hwc_display);
}

PixmapPtr
get_crtc_pixmap(hwc_display_ptr hwc_display) {
    PixmapPtr crtcPixmap;
    void *pixels = NULL;
    xf86CrtcPtr crtc = hwc_display->pCrtc;
    HWCPtr hwc = HWCPTR(crtc->scrn);
    int index = (int64_t) crtc->driver_private;

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "get_crtc_pixmap width: %d, height: %d, index: %d\n", hwc_display->width,
               hwc_display->height, index);

    if (hwc->glamor) {
#ifdef ENABLE_GLAMOR
        crtcPixmap = glamor_create_pixmap(crtc->scrn->pScreen,
                                            hwc_display->width,
                                            hwc_display->height,
                                            PIXMAN_FORMAT_DEPTH(HYBRIS_PIXEL_FORMAT_RGBA_8888),
                                            GLAMOR_CREATE_NO_LARGE);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "created glamor pixmap: %p, index: %d\n", crtcPixmap, index);
#endif
    } else {
        crtcPixmap = (*crtc->scrn->pScreen->CreatePixmap)(crtc->scrn->pScreen,
                                                          hwc_display->width,
                                                          hwc_display->height,
                                                          crtc->scrn->depth,
                                                          0);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "created pixmap: %p, index: %d\n", crtcPixmap, index);
    }

    int err = hwc->egl_proc.eglHybrisCreateNativeBuffer(hwc_display->width, hwc_display->height,
                                                        HYBRIS_USAGE_HW_TEXTURE |
                                                        HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                        HYBRIS_PIXEL_FORMAT_RGBA_8888,
                                                        &hwc_display->stride, &hwc_display->buffer);

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "alloc: status=%d, stride=%d\n", err, hwc_display->stride);

    if (hwc->glamor) {
#ifdef ENABLE_GLAMOR
        hwc_display->hwc_renderer.rootTexture = glamor_get_pixmap_texture(crtcPixmap);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "glamor_get_pixmap_texture\n");
#endif
    }

    err = hwc->egl_proc.eglHybrisLockNativeBuffer(hwc_display->buffer,
                                                  HYBRIS_USAGE_SW_READ_OFTEN | HYBRIS_USAGE_SW_WRITE_OFTEN,
                                                  0, 0, hwc_display->stride, hwc_display->height, &pixels);

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
        hwc->dirty = FALSE;
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "Damage tracking initialized, index: %d\n", index);
    } else {
        xf86DrvMsg(crtc->scrn->scrnIndex, X_ERROR, "Failed to create screen damage record, index: %d\n", index);
        return FALSE;
    }

    return crtcPixmap;
}

void crtc_pixmap_destroy(hwc_display_ptr hwc_display, PixmapPtr pPixmap);

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

    crtc_pixmap_destroy(hwc_display, pPixmap);
}

void
crtc_pixmap_destroy(hwc_display_ptr hwc_display, PixmapPtr pPixmap) {
    HWCPtr hwc = HWCPTR(hwc_display->pCrtc->scrn);

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
    crtc->mode = *mode;
    crtc->x = x;
    crtc->y = y;
    crtc->rotation = rotation;
    int index = (int64_t) crtc->driver_private;
    hwc_display_ptr hwc_display;
    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }
    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
               "hwc_set_mode_major as setting x: %d, y: %d, desiredX: %d, desiredY: %d, rotation: %d, initialR: %d, driverIsPerformingTransform: %d, index: %d\n",
               x, y, crtc->desiredX, crtc->desiredY, rotation, hwc_display->pOutput->initial_rotation, crtc->driverIsPerformingTransform, index);

    if (hwc_display->rotationOnFirstSetMode) {
        crtc->rotation = hwc_display->pOutput->initial_rotation;
        hwc_display->rotationOnFirstSetMode = FALSE;
    }
    if (index == HWC_DISPLAY_PRIMARY) {
        char buf[100];
        snprintf(buf, sizeof buf, "echo %d > /sys/devices/platform/touch/screen_rotation", crtc->rotation);
        int ret = system(buf);
        xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO,
                   "hwc_set_mode_major updated touch rotation ret: %d, dpmsMode: %d, device_open: %d\n", ret,
                   hwc_display->dpmsMode, hwc->device_open);

        if (hwc_display->dpmsMode != DPMSModeOn && hwc->device_open) {
            xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "calling hwc_output_set_mode\n");
            hwc_output_set_mode(crtc->scrn, hwc_display, index, DPMSModeOn);
        }
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

    hwc_trigger_redraw(crtc->scrn);
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

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_hide_cursor index: %d\n", index);

    hwc_trigger_redraw(crtc->scrn);
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

    xf86DrvMsg(crtc->scrn->scrnIndex, X_INFO, "hwc_show_cursor index: %d\n", index);

    hwc_trigger_redraw(crtc->scrn);
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
// .set_origin?
//        .gamma_set = dummy_crtc_gamma_set,
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
    return NULL;
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

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_dpms mode: %d, index: %d, connected: %d\n", mode, index,
               hwc->connected_outputs & (1 << index));

    if (index == 1 && !(hwc->connected_outputs & (1 << index) && hwc->external_initialised)) {
        return;
    }

    hwc_display->dpmsMode = mode;

    if (mode != DPMSModeOn) {
        // Wait for the renderer thread to finish to avoid causing locks
        pthread_mutex_lock(&(hwc->dirtyLock));
        hwc->dirty = FALSE;
        pthread_mutex_unlock(&(hwc->dirtyLock));
    }

    ScrnInfoPtr pScrn;
    pScrn = output->scrn;
    hwc_output_set_mode(pScrn, hwc_display, index, mode);
}

void
hwc_output_set_mode(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, int index, int mode) {
    HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_output_set_mode mode: %d, index: %d\n", mode, index);

    hwc_display->dpmsMode = mode;

    pthread_mutex_lock(&(hwc->rendererLock));
    hwc_set_power_mode(pScrn, index, mode);
    pthread_mutex_unlock(&(hwc->rendererLock));

    if (index == HWC_DISPLAY_PRIMARY) {
        hwc_toggle_screen_brightness(pScrn);
    }

    if (mode == DPMSModeOn) {
        // Force redraw after unblank
        hwc_trigger_redraw(pScrn);
    }
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
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_mode_set - connected_outputs: %d\n", hwc->connected_outputs);
}

/* The first virtual monitor is always connected. Others only after setting its mode */
static xf86OutputStatus
hwc_output_detect(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_detect index: %d, extId: %ld\n", index, hwc->external_display_id);

    if (hwc->connected_outputs & (1 << index)) {
//        if (index == 1 && hwc->external_display_id > 0) {
//            hwc_display_init(output->scrn, &hwc->external_display, hwc->hwc2Device, hwc->external_display_id);
//            hwc->external_display_id = 0;
//        }
        return XF86OutputStatusConnected;
    } else {
        return XF86OutputStatusDisconnected;
    }
}

DisplayModePtr
hwc_output_get_modes(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "hwc_output_get_modes - pW: %d, pH: %d, eW: %d, eH:%d index: %d\n",
               hwc->primary_display.width, hwc->primary_display.height, hwc->external_display.width,
               hwc->external_display.height, index);

    DisplayModePtr mode;
    if (index == 0) {
        mode = xf86CVTMode(hwc->primary_display.width, hwc->primary_display.height, 60, 0, 0);
    } else {
        mode = xf86CVTMode(hwc->external_display.width, hwc->external_display.height, 60, 0, 0);
    }
    mode->type = M_T_DRIVER | M_T_PREFERRED;
    return mode;
}

void dummy_output_register_prop(xf86OutputPtr output, Atom prop, uint64_t value) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;

    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_register_prop %d\n", index);
}

void dummy_output_create_resources(xf86OutputPtr output) {
    HWCPtr hwc = HWCPTR(output->scrn);
    int index = (int64_t) output->driver_private;
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_create_resources %d\n", index);
}

static Bool dummy_output_set_property(xf86OutputPtr output, Atom property,
                                      RRPropertyValuePtr value) {
    xf86DrvMsg(output->scrn->scrnIndex, X_INFO, "dummy_output_set_property prop: %d, valT: %d\n", property,
               value->type);
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
hwc_trigger_redraw(ScrnInfoPtr pScrn) {
    HWCPtr hwc = HWCPTR(pScrn);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_trigger_redraw dirty: %d\n", hwc->dirty);

    if (hwc->dirty)
        return;

    pthread_mutex_lock(&(hwc->dirtyLock));
    hwc->dirty = TRUE;
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

        xf86CrtcPtr crtcPtr = xf86CrtcCreate(pScrn, &hwcomposer_crtc_funcs);
        crtcPtr->driver_private = (void *) (uintptr_t) i;
        //XF86DriverTransformOutput
        crtcPtr->driverIsPerformingTransform = XF86DriverTransformCursorImage+XF86DriverTransformCursorPosition;

        snprintf(szOutput, sizeof(szOutput), "Screen%u", i);
        xf86OutputPtr outputPtr = xf86OutputCreate(pScrn, &hwcomposer_output_funcs, szOutput);
        outputPtr->possible_crtcs = 1 << i;
        outputPtr->possible_clones = 0;
        outputPtr->driver_private = (void *) (uintptr_t) i;

        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Created crtc (%p) and output %s (%p)\n",
                   (void *) crtcPtr, szOutput,
                   (void *) outputPtr);

        if (i == 0) {
//            xf86OutputUseScreenMonitor(outputPtr, TRUE);
            hwc->primary_display.pCrtc = crtcPtr;
            hwc->primary_display.pOutput = outputPtr;
            hwc->primary_display.rotationOnFirstSetMode = TRUE;
        } else {
//            xf86OutputUseScreenMonitor(outputPtr, FALSE);
            hwc->external_display.pCrtc = crtcPtr;
            hwc->external_display.pOutput = outputPtr;
            hwc->external_display.rotationOnFirstSetMode = TRUE;
        }
    }

    if (hwc->device_open > 0) {
        hwc->connected_outputs |= 1 << 0;
    }
    if (hwc->usb_hdmi_plugged > 0 && !hwc->external_initialised) {
        hwc_egl_renderer_external_power_up(pScrn);
    }

    // Pick rotated HWComposer screen resolution
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init primary picked\n");

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init dx: %d, w: %d, h: %d, outputs: %d\n",
               pScrn->display->virtualX, pScrn->virtualX, pScrn->virtualY, hwc->connected_outputs);

    xf86ProviderSetup(pScrn, NULL, "hwcomposer");

    // Now create our initial CRTC/output configuration.
    if (!xf86InitialConfiguration(pScrn, TRUE)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "Initial CRTC configuration failed!\n");
        return (FALSE);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_pre_init dx: %d, w: %d, h: %d, outputs: %d\n",
               pScrn->display->virtualX, pScrn->virtualX, pScrn->virtualY, hwc->connected_outputs);

    pScrn->currentMode = pScrn->modes;

    return TRUE;
}
