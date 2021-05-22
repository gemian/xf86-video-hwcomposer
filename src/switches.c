/*
 * Copyright © 2021 Gemian
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libudev.h>
#include <xorg/os.h>

#include "driver.h"

#define SWTICH_DEV_TYPE "switch"

Bool device_open = FALSE;

static void udev_callback(int fd, int ready, void *data)
{
    udev_switches_data_ptr dataPtr = data;
    HWCPtr hwc = HWCPTR(dataPtr->pScrn);

    struct udev_device *device = udev_monitor_receive_device(dataPtr->monitor);
    if (!device) {
        xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback udev_monitor_receive_device failed\n");
        return;
    }

    const char *dev_path = udev_device_get_devpath(device);
    if (!dev_path) {
        xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback udev_device_get_devpath failed\n");
        udev_device_unref(device);
        return;
    }

    struct udev_list_entry *entry = udev_list_entry_get_by_name(udev_device_get_properties_list_entry(device),
                                                                "SWITCH_STATE");
    if (!entry) {
        xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback udev_list_entry_get_by_name failed\n");
        udev_device_unref(device);
        return;
    }

    const char *state = udev_list_entry_get_value(entry);
    if (!state) {
        xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback udev_list_entry_get_value failed\n");
        udev_device_unref(device);
        return;
    }

    xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback path: %s state: %s\n", dev_path, state);

    char *end;
    const long state_long = strtol(state, &end, 10);
    if (state == end) {
        xf86DrvMsg(dataPtr->pScrn->scrnIndex, X_INFO, "udev_callback strtol failed\n");
        udev_device_unref(device);
        return;
    }

    if (!strcmp(dev_path, "/devices/virtual/" SWTICH_DEV_TYPE "/hall")) {
        device_open = state_long;
        if (hwc->primary_display.dpmsMode != DPMSModeOn && device_open) {
            hwc_output_set_mode(dataPtr->pScrn, &hwc->primary_display, HWC_DISPLAY_PRIMARY, DPMSModeOn);
        } else if (hwc->primary_display.dpmsMode == DPMSModeOn && !device_open) {
            hwc_output_set_mode(dataPtr->pScrn, &hwc->primary_display, HWC_DISPLAY_PRIMARY, DPMSModeOff);
        }
    } else if (!strcmp(dev_path, "/devices/virtual/" SWTICH_DEV_TYPE "/usb_hdmi")) {
        if (state_long == 1) {
            hwc_egl_renderer_external_power_up(dataPtr->pScrn);
        }
    }
    udev_device_unref(device);
}

Bool init_udev_switches(udev_switches_data_ptr data)
{
    if (!data) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches no data offered\n");
        return FALSE;
    }

    data->udev = udev_new();
    if (!data->udev) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches udev_new failed\n");
        return FALSE;
    }

    data->monitor = udev_monitor_new_from_netlink(data->udev, "udev");
    if (!data->monitor) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches udev_monitor_new_from_netlink failed\n");
        return FALSE;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(data->monitor, SWTICH_DEV_TYPE, NULL) < 0) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO,
                   "init_udev_switches udev_monitor_filter_add_match_subsystem_devtype switch failed\n");
        return FALSE;
    }

    if (udev_monitor_enable_receiving(data->monitor) < 0) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches udev_monitor_enable_receiving failed\n");
        return FALSE;
    }

    int monitor_fd = udev_monitor_get_fd(data->monitor);
    if (!monitor_fd) {
        xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches udev_monitor_get_fd failed\n");
        return FALSE;
    }

    xf86DrvMsg(data->pScrn->scrnIndex, X_INFO, "init_udev_switches calling SetNotifyFd\n");
    SetNotifyFd(monitor_fd, udev_callback, X_NOTIFY_READ, data);

    return TRUE;
}

Bool close_udev_switches(udev_switches_data_ptr data)
{
    RemoveNotifyFd(udev_monitor_get_fd(data->monitor));
    udev_monitor_unref(data->monitor);
    data->monitor = NULL;
    udev_unref(data->udev);
    data->udev = NULL;
}