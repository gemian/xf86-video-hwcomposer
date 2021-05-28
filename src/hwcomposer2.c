#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include "xf86.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <malloc.h>
#include <dlfcn.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <android-config.h>
#include <sync/sync.h>
#include <hybris/hwcomposerwindow/hwcomposer.h>
#include <hybris/hwc2/hwc2_compatibility_layer.h>

#include "driver.h"

typedef struct
{
    struct HWC2EventListener listener;
    ScrnInfoPtr pScrn;
} HwcProcs_v20;

void hwc2_callback_vsync(HWC2EventListener *listener, int32_t sequenceId,
                         hwc2_display_t display_id, int64_t timestamp)
{
}

void hwc2_callback_hotplug(HWC2EventListener *listener, int32_t sequenceId,
                           hwc2_display_t display_id, bool connected,
                           bool primaryDisplay)
{
    ScrnInfoPtr pScrn = ((HwcProcs_v20 *) listener)->pScrn;
    HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "onHotplugReceived(%d, %" PRIu64 ", %s, %s)\n",
        sequenceId, display_id,
        connected ? "connected" : "disconnected",
        primaryDisplay ? "primary" : "external");
    if (!primaryDisplay) {
        hwc->external_display_id = display_id;
        hwc->connected_outputs = connected ? 0b11 : 1; //bitmask
        hwc_trigger_redraw(pScrn, &hwc->external_display);
    }
    hwc2_compat_device_on_hotplug(hwc->hwc2Device, display_id, connected);
}

void hwc2_callback_refresh(HWC2EventListener *listener, int32_t sequenceId,
                           hwc2_display_t display_id)
{
    xf86DrvMsg(0, X_INFO, "hwc2_callback_refresh (%p, %d, %ld)\n", listener, sequenceId, display_id);
}

#define HDMI_DRV "/dev/hdmitx"

#define HDMI_IOW(num, dtype)     _IOW('H', num, dtype)
#define HDMI_IOWR(num, dtype)    _IOWR('H', num, dtype)
#define HDMI_IO(num)             _IO('H', num)

#define MTK_HDMI_AUDIO_VIDEO_ENABLE             HDMI_IO(1)
#define MTK_HDMI_POWER_ENABLE                   HDMI_IOW(12, int)
#define MTK_HDMI_VIDEO_CONFIG                   HDMI_IOWR(6, int)

static Bool hdmi_ioctl(int code, long value)
{
    int fd = open(HDMI_DRV, O_RDONLY, 0);
    int ret = -1;
    if (fd >= 0) {
        ret = ioctl(fd, code, value);
        if (ret < 0) {
            xf86DrvMsg(0, X_INFO, "[HDMI] [%s] failed. ioctlCode: %d, errno: %d\n",
                       __func__, code, errno);
        }
        close(fd);
    } else {
        xf86DrvMsg(0, X_INFO, "[HDMI] [%s] open hdmitx failed. errno: %d\n", __func__, errno);
    }
    if (ret < 0) {
        return FALSE;
    } else {
        return TRUE;
    }
}

Bool hdmi_power_enable(Bool enable) {
    return hdmi_ioctl(MTK_HDMI_POWER_ENABLE, enable);
}

Bool hdmi_enable(Bool enable) {
    return hdmi_ioctl(MTK_HDMI_AUDIO_VIDEO_ENABLE, enable);
}

Bool hdmi_set_video_config(int vformat)  {
    return hdmi_ioctl(MTK_HDMI_VIDEO_CONFIG, vformat);
}

extern const EGLint egl_attr[];

void present(void *user_data, struct ANativeWindow *window,
             struct ANativeWindowBuffer *buffer);

void hwc_present_hwcomposer2(void *user_data, struct ANativeWindow *window,
                             struct ANativeWindowBuffer *buffer);

Bool hwc_display_init(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, hwc2_compat_device_t* hwc2_compat_device, int id) {
    HWCPtr hwc = HWCPTR(pScrn);
    EGLint num_config;
    EGLBoolean rv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init id: %d\n", id);
    for (int i = 0; i < 5 * 1000; ++i) {
        // Wait at most 5s for hotplug events
        if ((hwc_display->hwc2_compat_display = hwc2_compat_device_get_display_by_id(hwc2_compat_device, id)))
            break;
        usleep(1000);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init got display: %p\n", hwc_display->hwc2_compat_display);
    if (!hwc_display->hwc2_compat_display) {
        return FALSE;
    }

    hwc2_compat_display_set_power_mode(hwc_display->hwc2_compat_display, HWC2_POWER_MODE_ON);

    HWC2DisplayConfig *config = hwc2_compat_display_get_active_config(hwc_display->hwc2_compat_display);
    if (!config) {
        return FALSE;
    }

    hwc_display->width = config->width;
    hwc_display->height = config->height;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init width: %i height: %i, id: %d\n", hwc_display->width,
               hwc_display->height, id);

    hwc_display->hwc2_compat_layer = hwc2_compat_display_create_layer(hwc_display->hwc2_compat_display);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init created layer\n");

    hwc2_compat_layer_set_composition_type(hwc_display->hwc2_compat_layer, HWC2_COMPOSITION_CLIENT);
    hwc2_compat_layer_set_blend_mode(hwc_display->hwc2_compat_layer, HWC2_BLEND_MODE_NONE);
    hwc2_compat_layer_set_source_crop(hwc_display->hwc2_compat_layer, 0.0f, 0.0f, hwc_display->width,
                                      hwc_display->height);
    hwc2_compat_layer_set_display_frame(hwc_display->hwc2_compat_layer, 0, 0, hwc_display->width, hwc_display->height);
    hwc2_compat_layer_set_visible_region(hwc_display->hwc2_compat_layer, 0, 0, hwc_display->width, hwc_display->height);

    if (hwc->hwcVersion < HWC_DEVICE_API_VERSION_2_0) {
        hwc_display->win = HWCNativeWindowCreate(hwc_display->width, hwc_display->height, HAL_PIXEL_FORMAT_RGBA_8888,
                                                 present, hwc_display);
    } else {
        hwc_display->win = HWCNativeWindowCreate(hwc_display->width, hwc_display->height, HAL_PIXEL_FORMAT_RGBA_8888,
                                                 hwc_present_hwcomposer2, hwc_display);
    }
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init created window: %p\n",hwc_display->win);

    if (hwc->egl_display == NULL) {
        hwc->egl_display = eglGetDisplay(NULL);
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc->egl_display != EGL_NO_DISPLAY);

        rv = eglInitialize(hwc->egl_display, 0, 0);
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);

        eglChooseConfig((EGLDisplay) hwc->egl_display, egl_attr, &hwc->egl_cfg, 1, &num_config);
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init eglGetDisplay+eglChooseConfig done eglD: %p\n", hwc->egl_display);
    } else {
        eglChooseConfig((EGLDisplay) hwc->egl_display, egl_attr, &hwc->egl_cfg, 1, &num_config);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init eglChooseConfig done eglD: %p\n", hwc->egl_display);
    }

    if (!hwc_display->hwc_renderer.surface) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init creating window surface\n");
        hwc_display->hwc_renderer.surface = eglCreateWindowSurface((EGLDisplay) hwc->egl_display, hwc->egl_cfg,
                                                                   (EGLNativeWindowType) hwc_display->win, NULL);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init - surface: %p\n", hwc_display->hwc_renderer.surface);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init - eglError: %d\n", eglGetError());
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc_display->hwc_renderer.surface != EGL_NO_SURFACE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init created window surface\n");
    }

    free(config);

    return TRUE;
}

Bool hwc_hwcomposer2_init(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);
    static int composerSequenceId = 0;

    HwcProcs_v20 *procs = malloc(sizeof(HwcProcs_v20));
    procs->listener.on_vsync_received = hwc2_callback_vsync;
    procs->listener.on_hotplug_received = hwc2_callback_hotplug;
    procs->listener.on_refresh_received = hwc2_callback_refresh;
    procs->pScrn = pScrn;

    hwc->hwc2Device = hwc2_compat_device_new(false);
    assert(hwc->hwc2Device);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_hwcomposer2_init outputs: %d\n", hwc->connected_outputs);

    hwc2_compat_device_register_callback(hwc->hwc2Device, &procs->listener, composerSequenceId++);

    return hwc_display_init(pScrn, &hwc->primary_display, hwc->hwc2Device, 0);
}

void hwc_hwcomposer2_close(ScrnInfoPtr pScrn)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_hwcomposer2_close\n");
}

int lastPresentFence = -1;

void hwc_present_hwcomposer2(void *user_data, struct ANativeWindow *window,
                             struct ANativeWindowBuffer *buffer)
{
    hwc_display_ptr hwc_display = (hwc_display_ptr) user_data;

    uint32_t numTypes = 0;
    uint32_t numRequests = 0;
    hwc2_error_t error = HWC2_ERROR_NONE;

    hwc2_compat_display_t *hwc2_compat_display = hwc_display->hwc2_compat_display;

    error = hwc2_compat_display_validate(hwc2_compat_display, &numTypes, &numRequests);
    if (error != HWC2_ERROR_NONE && error != HWC2_ERROR_HAS_CHANGES) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: validate failed: %d\n", error);
        return;
    }

    if (numTypes || numRequests) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: validate required changes: %d\n", error);
        return;
    }

    error = hwc2_compat_display_accept_changes(hwc2_compat_display);
    if (error != HWC2_ERROR_NONE) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: acceptChanges failed: %d\n", error);
        return;
    }

    int acquireFenceFd = HWCNativeBufferGetFence(buffer);

    hwc2_compat_display_set_client_target(hwc2_compat_display, /* slot */0, buffer,
                                          acquireFenceFd,
                                          HAL_DATASPACE_UNKNOWN);

    int presentFence = -1;
    hwc2_compat_display_present(hwc2_compat_display, &presentFence);
    xf86DrvMsg(hwc_display->index, X_INFO, "hwc_present_hwcomposer2 pF: %d\n", presentFence);

//    hwc2_compat_out_fences_t* fences;
//    err = hwc2_compat_display_get_release_fences(display, &fences);
//    if (err != HWC2_ERROR_NONE) {
//        xf86DrvMsg(hwc_display->index, X_INFO, "hwc_present_hwcomposer2 presentAndGetReleaseFences: Failed to get release fences for display %d: %d\n", (uint32_t) display_id, err);
//        return;
//    }
//    int fenceFd = hwc2_compat_out_fences_get_fence(fences, layer);
//    if (fenceFd != -1)
//        setFenceBufferFd(buffer, fenceFd);
//    hwc2_compat_out_fences_destroy(fences);

    if (lastPresentFence != -1) {
        sync_wait(lastPresentFence, -1);
        close(lastPresentFence);
    }

    lastPresentFence = presentFence != -1 ? dup(presentFence) : -1;

    HWCNativeBufferSetFence(buffer, presentFence);
}

void hwc_set_power_mode_hwcomposer2(ScrnInfoPtr pScrn, int disp, int mode)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_set_power_mode_hwcomposer2 disp: %d, mode: %d\n", disp, mode);

    HWCPtr hwc = HWCPTR(pScrn);
    hwc_display_ptr hwc_display = NULL;
    if (disp == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }

    if (mode == DPMSModeOff && lastPresentFence != -1) {
        sync_wait(lastPresentFence, -1);
        close(lastPresentFence);
        lastPresentFence = -1;
    }

    if (mode == DPMSModeOn) {
        hwc2_compat_display_set_power_mode(hwc_display->hwc2_compat_display, HWC2_POWER_MODE_ON);
    } else {
        hwc2_compat_display_set_power_mode(hwc_display->hwc2_compat_display, HWC2_POWER_MODE_OFF);
    }
}
