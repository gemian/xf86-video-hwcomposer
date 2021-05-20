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

void hwc2_callback_vsync(HWC2EventListener* listener, int32_t sequenceId,
                         hwc2_display_t display_id, int64_t timestamp)
{
}

void hwc2_callback_hotplug(HWC2EventListener* listener, int32_t sequenceId,
                           hwc2_display_t display_id, bool connected,
                           bool primaryDisplay)
{
    ScrnInfoPtr pScrn = ((HwcProcs_v20*) listener)->pScrn;
    HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "onHotplugReceived(%d, %" PRIu64 ", %s, %s)\n",
           sequenceId, display_id,
           connected ? "connected" : "disconnected",
           primaryDisplay ? "primary" : "external");
	if (!primaryDisplay) {
        hwc->external_display_id = display_id;
		hwc->connected_outputs = connected ? 0b11 : 1; //bitmask
	}
    hwc2_compat_device_on_hotplug(hwc->hwc2Device, display_id, connected);
}

void hwc2_callback_refresh(HWC2EventListener* listener, int32_t sequenceId,
                           hwc2_display_t display_id)
{
	xf86DrvMsg(0, X_INFO, "hwc2_callback_refresh (%p, %d, %ld)\n", listener, sequenceId, display_id);
}

Bool hwc_display_init(ScrnInfoPtr pScrn, hwc_display_ptr display, hwc2_compat_device_t* hwc2_compat_device, int id) {
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init id: %d\n", id);
	for (int i = 0; i < 5 * 1000; ++i) {
		// Wait at most 5s for hotplug events
		if ((display->hwc2_compat_display = hwc2_compat_device_get_display_by_id(hwc2_compat_device, id)))
			break;
		usleep(1000);
	}
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init got display: %p\n", display->hwc2_compat_display);
	assert(display->hwc2_compat_display);

	HWC2DisplayConfig *config = hwc2_compat_display_get_active_config(display->hwc2_compat_display);
	assert(config);

	display->width = config->width;
	display->height = config->height;
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_display_init width: %i height: %i, id: %d\n", display->width, display->height, id);
	free(config);

	hwc2_compat_layer_t* layer = display->hwc2_compat_layer = hwc2_compat_display_create_layer(display->hwc2_compat_display);

	display->lastPresentFence = -1;

	hwc2_compat_layer_set_composition_type(layer, HWC2_COMPOSITION_CLIENT);
	hwc2_compat_layer_set_blend_mode(layer, HWC2_BLEND_MODE_NONE);
	hwc2_compat_layer_set_source_crop(layer, 0.0f, 0.0f, config->width, config->height);
	hwc2_compat_layer_set_display_frame(layer, 0, 0, config->width, config->height);
	hwc2_compat_layer_set_visible_region(layer, 0, 0, config->width, config->height);

	return TRUE;
}

Bool hwc_hwcomposer2_init(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);
    static int composerSequenceId = 0;
    
    HwcProcs_v20* procs = malloc(sizeof(HwcProcs_v20));
    procs->listener.on_vsync_received = hwc2_callback_vsync;
    procs->listener.on_hotplug_received = hwc2_callback_hotplug;
    procs->listener.on_refresh_received = hwc2_callback_refresh;
    procs->pScrn = pScrn;

    hwc2_compat_device_t* hwc2_device = hwc->hwc2Device = hwc2_compat_device_new(false);
    assert(hwc2_device);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_hwcomposer2_init outputs: %d\n", hwc->connected_outputs);

    hwc2_compat_device_register_callback(hwc2_device, &procs->listener,
        composerSequenceId++);

    return hwc_display_init(pScrn, &hwc->primary_display, hwc2_device, 0);
}

void hwc_hwcomposer2_close(ScrnInfoPtr pScrn)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_hwcomposer2_close\n");
}

void hwc_present_hwcomposer2(void *user_data, struct ANativeWindow *window,
								struct ANativeWindowBuffer *buffer)
{
	hwc_display_ptr hwc_display = (hwc_display_ptr)user_data;

	uint32_t numTypes = 0;
    uint32_t numRequests = 0;
    hwc2_error_t error = HWC2_ERROR_NONE;

    int acquireFenceFd = HWCNativeBufferGetFence(buffer);

    hwc2_compat_display_t* hwc2_compat_display = hwc_display->hwc2_compat_display;

    error = hwc2_compat_display_validate(hwc2_compat_display, &numTypes,
                                                    &numRequests);
    if (error != HWC2_ERROR_NONE && error != HWC2_ERROR_HAS_CHANGES) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: validate failed: %d", error);
        return;
    }

    if (numTypes || numRequests) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: validate required changes: %d", error);
        return;
    }

    error = hwc2_compat_display_accept_changes(hwc2_compat_display);
    if (error != HWC2_ERROR_NONE) {
        xf86DrvMsg(hwc_display->index, X_ERROR, "prepare: acceptChanges failed: %d", error);
        return;
    }

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

    if (hwc_display->lastPresentFence != -1) {
        sync_wait(hwc_display->lastPresentFence, -1);
        close(hwc_display->lastPresentFence);
    }

	hwc_display->lastPresentFence = presentFence != -1 ? dup(presentFence) : -1;

    HWCNativeBufferSetFence(buffer, presentFence);
}

void hwc_set_power_mode_hwcomposer2(ScrnInfoPtr pScrn, int disp, int mode)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_set_power_mode_hwcomposer2 disp: %d, mode: %d\n", disp, mode);

    HWCPtr hwc = HWCPTR(pScrn);
	hwc_display_ptr hwc_display = NULL;
	if (disp == 0) {
		hwc_display = &hwc->primary_display;
	} else {
		hwc_display = &hwc->external_display;
	}

	if (mode == DPMSModeOff && hwc_display->lastPresentFence != -1) {
        sync_wait(hwc_display->lastPresentFence, -1);
        close(hwc_display->lastPresentFence);
		hwc_display->lastPresentFence = -1;
    }

	hwc2_compat_display_set_power_mode(hwc_display->hwc2_compat_display, (mode) ? HWC2_POWER_MODE_ON : HWC2_POWER_MODE_OFF);
}
