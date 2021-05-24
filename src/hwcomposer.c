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

#include "driver.h"

void *android_dlopen(const char *filename, int flags);
void *android_dlsym(void *handle, const char *symbol);
int android_dlclose(void *handle);

inline static uint32_t interpreted_version(hw_device_t *hwc_device)
{
	uint32_t version = hwc_device->version;

	if ((version & 0xffff0000) == 0) {
		// Assume header version is always 1
		uint32_t header_version = 1;

		// Legacy version encoding
		version = (version << 16) | header_version;
	}
	return version;
}

void hwc_set_power_mode(ScrnInfoPtr pScrn, int disp, int mode)
{
	HWCPtr hwc = HWCPTR(pScrn);
    
	if (hwc->hwcVersion == HWC_DEVICE_API_VERSION_2_0) {
		hwc_set_power_mode_hwcomposer2(pScrn, disp, mode);
	} else {
		hwc_composer_device_1_t *hwcDevicePtr = hwc->primary_display.hwcDevicePtr;
		if (disp == HWC_DISPLAY_EXTERNAL) {
			hwcDevicePtr = hwc->external_display.hwcDevicePtr;
		}
		hw_device_t * hwcDevice = &hwcDevicePtr->common;

		if (hwc->hwcVersion == HWC_DEVICE_API_VERSION_1_5 || hwc->hwcVersion == HWC_DEVICE_API_VERSION_1_5) {
			hwcDevicePtr->setPowerMode(hwcDevicePtr, disp, (mode == DPMSModeOn) ? HWC_POWER_MODE_NORMAL : HWC_POWER_MODE_OFF);
		} else {
			hwcDevicePtr->blank(hwcDevicePtr, disp, (mode == DPMSModeOn) ? 0 : 1);
		}
	}
}

void hwc_start_fake_surfaceflinger(ScrnInfoPtr pScrn) {
	HWCPtr hwc = HWCPTR(pScrn);
	void (*startMiniSurfaceFlinger)(void) = NULL;

	// Adapted from https://github.com/mer-hybris/qt5-qpa-hwcomposer-plugin/blob/master/hwcomposer/hwcomposer_backend.cpp#L88

	// A reason for calling this method here is to initialize the binder
	// thread pool such that services started from for example the
	// hwcomposer plugin don't get stuck.

	hwc->libminisf = android_dlopen("libminisf.so", RTLD_LAZY);

	if (hwc->libminisf) {
		startMiniSurfaceFlinger = (void(*)(void))android_dlsym(hwc->libminisf, "startMiniSurfaceFlinger");
	}

	if (startMiniSurfaceFlinger) {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "starting mini surface flinger\n");
		startMiniSurfaceFlinger();
	} else {
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "libminisf is incompatible or missing. Can not possibly start the fake SurfaceFlinger service.\n");
	}
}

Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	int err;

	hwc_start_fake_surfaceflinger(pScrn);

	hw_module_t *hwcModule = 0;
	err = hw_get_module(HWC_HARDWARE_MODULE_ID, (const hw_module_t **) &hwcModule);
	assert(err == 0);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "== hwcomposer module ==\n");
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * Address: %p\n", hwcModule);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * Module API Version: %x\n", hwcModule->module_api_version);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * HAL API Version: %x\n", hwcModule->hal_api_version); /* should be zero */
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * Identifier: %s\n", hwcModule->id);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * Name: %s\n", hwcModule->name);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, " * Author: %s\n", hwcModule->author);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "== hwcomposer module ==\n");

	hw_device_t *hwcDevice = NULL;
	err = hwcModule->methods->open(hwcModule, HWC_HARDWARE_COMPOSER, &hwcDevice);
	if (err) {
		// For weird reason module open seems to currently fail on tested HWC2 device
		hwc->hwcVersion = HWC_DEVICE_API_VERSION_2_0;
	} else {
		hwc->hwcVersion = interpreted_version(hwcDevice);
	}

    if (hwc->hwcVersion == HWC_DEVICE_API_VERSION_2_0)
		return hwc_hwcomposer2_init(pScrn);
	
	hwc_composer_device_1_t *hwcDevicePtr = (hwc_composer_device_1_t*) hwcDevice;
	hwc->primary_display.hwcDevicePtr = hwcDevicePtr;
	hwc_set_power_mode(pScrn, HWC_DISPLAY_PRIMARY, DPMSModeOn);

	uint32_t configs[5];
	size_t numConfigs = 5;

	err = hwcDevicePtr->getDisplayConfigs(hwcDevicePtr, HWC_DISPLAY_PRIMARY, configs, &numConfigs);
	assert (err == 0);

	int32_t attr_values[2];
	uint32_t attributes[] = { HWC_DISPLAY_WIDTH, HWC_DISPLAY_HEIGHT, HWC_DISPLAY_NO_ATTRIBUTE };

	hwcDevicePtr->getDisplayAttributes(hwcDevicePtr, HWC_DISPLAY_PRIMARY,
			configs[0], attributes, attr_values);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "width: %i height: %i\n", attr_values[0], attr_values[1]);
	hwc->primary_display.width = attr_values[0];
	hwc->primary_display.height = attr_values[1];

	size_t size = sizeof(hwc_display_contents_1_t) + 2 * sizeof(hwc_layer_1_t);
	hwc_display_contents_1_t *list = (hwc_display_contents_1_t *) malloc(size);
	hwc->primary_display.hwcContents = (hwc_display_contents_1_t **) malloc(HWC_NUM_DISPLAY_TYPES * sizeof(hwc_display_contents_1_t *));
	const hwc_rect_t r = { 0, 0, attr_values[0], attr_values[1] };

	int counter = 0;
	for (; counter < HWC_NUM_DISPLAY_TYPES; counter++)
		hwc->primary_display.hwcContents[counter] = NULL;
	// Assign the layer list only to the first display,
	// otherwise HWC might freeze if others are disconnected
	hwc->primary_display.hwcContents[0] = list;

	hwc_layer_1_t *layer = &list->hwLayers[0];
	memset(layer, 0, sizeof(hwc_layer_1_t));
	layer->compositionType = HWC_FRAMEBUFFER;
	layer->hints = 0;
	layer->flags = 0;
	layer->handle = 0;
	layer->transform = 0;
	layer->blending = HWC_BLENDING_NONE;
#ifdef HWC_DEVICE_API_VERSION_1_3
	layer->sourceCropf.top = 0.0f;
	layer->sourceCropf.left = 0.0f;
	layer->sourceCropf.bottom = (float) attr_values[1];
	layer->sourceCropf.right = (float) attr_values[0];
#else
	layer->sourceCrop = r;
#endif
	layer->displayFrame = r;
	layer->visibleRegionScreen.numRects = 1;
	layer->visibleRegionScreen.rects = &layer->displayFrame;
	layer->acquireFenceFd = -1;
	layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
	// We've observed that qualcomm chipsets enters into compositionType == 6
	// (HWC_BLIT), an undocumented composition type which gives us rendering
	// glitches and warnings in logcat. By setting the planarAlpha to non-
	// opaque, we attempt to force the HWC into using HWC_FRAMEBUFFER for this
	// layer so the HWC_FRAMEBUFFER_TARGET layer actually gets used.
	int tryToForceGLES = getenv("QPA_HWC_FORCE_GLES") != NULL;
	layer->planeAlpha = tryToForceGLES ? 1 : 255;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	layer->surfaceDamage.numRects = 0;
#endif

	hwc->primary_display.fblayer = layer = &list->hwLayers[1];
	memset(layer, 0, sizeof(hwc_layer_1_t));
	layer->compositionType = HWC_FRAMEBUFFER_TARGET;
	layer->hints = 0;
	layer->flags = 0;
	layer->handle = 0;
	layer->transform = 0;
	layer->blending = HWC_BLENDING_NONE;
#ifdef HWC_DEVICE_API_VERSION_1_3
	layer->sourceCropf.top = 0.0f;
	layer->sourceCropf.left = 0.0f;
	layer->sourceCropf.bottom = (float) attr_values[1];
	layer->sourceCropf.right = (float) attr_values[0];
#else
	layer->sourceCrop = r;
#endif
	layer->displayFrame = r;
	layer->visibleRegionScreen.numRects = 1;
	layer->visibleRegionScreen.rects = &layer->displayFrame;
	layer->acquireFenceFd = -1;
	layer->releaseFenceFd = -1;
#if (ANDROID_VERSION_MAJOR >= 4) && (ANDROID_VERSION_MINOR >= 3) || (ANDROID_VERSION_MAJOR >= 5)
	layer->planeAlpha = 0xff;
#endif
#ifdef HWC_DEVICE_API_VERSION_1_5
	layer->surfaceDamage.numRects = 0;
#endif

	list->retireFenceFd = -1;
	list->flags = HWC_GEOMETRY_CHANGED;
	list->numHwLayers = 2;

    return TRUE;
}

void hwc_hwcomposer_close(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);

    close_udev_switches(&hwc->udev_switches);
}

static void present(void *user_data, struct ANativeWindow *window,
								struct ANativeWindowBuffer *buffer)
{
	hwc_display_ptr hwc_display = (hwc_display_ptr)user_data;

	hwc_display_contents_1_t **contents = hwc_display->hwcContents;
	hwc_layer_1_t *fblayer = hwc_display->fblayer;
	hwc_composer_device_1_t *hwcdevice = hwc_display->hwcDevicePtr;

	int oldretire = contents[0]->retireFenceFd;
	contents[0]->retireFenceFd = -1;

	fblayer->handle = buffer->handle;
	fblayer->acquireFenceFd = HWCNativeBufferGetFence(buffer);
	fblayer->releaseFenceFd = -1;
	int err = hwcdevice->prepare(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
	assert(err == 0);

	err = hwcdevice->set(hwcdevice, HWC_NUM_DISPLAY_TYPES, contents);
	/* in Android, SurfaceFlinger ignores the return value as not all
		display types may be supported */
	HWCNativeBufferSetFence(buffer, fblayer->releaseFenceFd);

	if (oldretire != -1)
	{
		sync_wait(oldretire, -1);
		close(oldretire);
	}
}

void hwc_present_hwcomposer2(void *user_data, struct ANativeWindow *window,
								struct ANativeWindowBuffer *buffer);

void hwc_get_native_window(HWCPtr hwc, hwc_display_ptr hwc_display) {

	xf86DrvMsg(hwc_display->pCrtc->scrn->scrnIndex, X_INFO, "hwc_get_native_window width: %d, height: %d\n", hwc_display->width, hwc_display->height);

	if (hwc->hwcVersion < HWC_DEVICE_API_VERSION_2_0) {
        hwc_display->win = HWCNativeWindowCreate(hwc_display->width, hwc_display->height, HAL_PIXEL_FORMAT_RGBA_8888, present, hwc_display);
	} else {
        hwc_display->win = HWCNativeWindowCreate(hwc_display->width, hwc_display->height, HAL_PIXEL_FORMAT_RGBA_8888, hwc_present_hwcomposer2, hwc_display);
	}
}

void hwc_toggle_screen_brightness(ScrnInfoPtr pScrn)
{
	HWCPtr hwc = HWCPTR(pScrn);
	struct light_state_t state;
	int brightness = 0;

	if (!hwc->lightsDevice) {
		return;
	}
	if (hwc->primary_display.dpmsMode == DPMSModeOn) {
        brightness = hwc->screenBrightness;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_toggle_screen_brightness brightness: %d, pDpms: %d, eDpms: %d, hwcBrightness: %d\n", brightness, hwc->primary_display.dpmsMode, hwc->external_display.dpmsMode, hwc->screenBrightness);

	state.flashMode = LIGHT_FLASH_NONE;
	state.brightnessMode = BRIGHTNESS_MODE_USER;

	state.color = (int)((0xffU << 24) | (brightness << 16) |
						(brightness << 8) | brightness);
	hwc->lightsDevice->set_light(hwc->lightsDevice, &state);
}
