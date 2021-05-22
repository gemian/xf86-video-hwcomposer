/* All drivers should typically include these */
#include "xf86.h"
#include "xf86_OSproc.h"

#include "xf86Cursor.h"
#include "xf86Crtc.h"

#ifdef XvExtension
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#endif
#include <string.h>
#include <pthread.h>

#include <android-config.h>

#define MESA_EGL_NO_X11_HEADERS 1
#include <epoxy/gl.h>
#include <epoxy/egl.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <hardware/lights.h>
#include <hybris/eglplatformcommon/hybris_nativebufferext.h>

#include <hybris/hwc2/hwc2_compatibility_layer.h>

#include "compat-api.h"

#define HWC_MAX_SCREENS 2

/* function prototypes */

extern Bool SwitchMode(SWITCH_MODE_ARGS_DECL);
extern void AdjustFrame(ADJUST_FRAME_ARGS_DECL);

/* globals */
typedef struct _color
{
    int red;
    int green;
    int blue;
} dummy_colors;

Bool hwc_display_pre_init(ScrnInfoPtr pScrn);
Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn);
Bool hwc_hwcomposer2_init(ScrnInfoPtr pScrn);
void hwc_hwcomposer_close(ScrnInfoPtr pScrn);
Bool hwc_lights_init(ScrnInfoPtr pScrn);
Bool hwc_drihybris_screen_init(ScreenPtr screen);

void hwc_toggle_screen_brightness(ScrnInfoPtr pScrn);
void hwc_set_power_mode(ScrnInfoPtr pScrn, int disp, int mode);
void hwc_set_power_mode_hwcomposer2(ScrnInfoPtr pScrn, int disp, int mode);

Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn);
void hwc_egl_renderer_close(ScrnInfoPtr pScrn);
void hwc_egl_renderer_screen_init(ScreenPtr pScreen, int disp);
void hwc_egl_renderer_screen_close(ScreenPtr pScreen, int disp);
void *hwc_egl_renderer_thread(void *user_data);

void hwc_ortho_2d(float* mat, float left, float right, float bottom, float top);
GLuint hwc_link_program(const GLchar *vert_src, const GLchar *frag_src);

Bool hwc_present_screen_init(ScreenPtr pScreen);
Bool hwc_cursor_init(ScreenPtr pScreen);

typedef enum {
	OPTION_ACCEL_METHOD,
	OPTION_EGL_PLATFORM,
	OPTION_SW_CURSOR,
    OPTION_ROTATE,
    OPTION_EXTERNAL_ROTATE
} Opts;

static const OptionInfoRec Options[] = {
        {OPTION_ACCEL_METHOD,    "AccelMethod",    OPTV_STRING,  {0}, FALSE},
        {OPTION_EGL_PLATFORM,    "EGLPlatform",    OPTV_STRING,  {0}, FALSE},
        {OPTION_SW_CURSOR,       "SWcursor",       OPTV_BOOLEAN, {0}, FALSE},
        {OPTION_ROTATE,          "Rotate",         OPTV_STRING,  {0}, FALSE},
        {OPTION_EXTERNAL_ROTATE, "ExternalRotate", OPTV_STRING,  {0}, FALSE},
        {-1,                     NULL,             OPTV_NONE,    {0}, FALSE}
};

typedef enum {
    HWC_ROTATE_NORMAL = RR_Rotate_0, // 1
    HWC_ROTATE_CW = RR_Rotate_90, // 2
    HWC_ROTATE_UD = RR_Rotate_180, // 4
    HWC_ROTATE_CCW = RR_Rotate_270 // 8
} hwc_rotation;

typedef struct {
	GLuint program;
    GLint position;
    GLint texcoords;
    GLint transform;
    GLint texture;
} hwc_renderer_shader;

typedef struct {
    PFNEGLHYBRISCREATENATIVEBUFFERPROC eglHybrisCreateNativeBuffer;
    PFNEGLHYBRISCREATEREMOTEBUFFERPROC eglHybrisCreateRemoteBuffer;
    PFNEGLHYBRISLOCKNATIVEBUFFERPROC eglHybrisLockNativeBuffer;
    PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC eglHybrisUnlockNativeBuffer;
    PFNEGLHYBRISRELEASENATIVEBUFFERPROC eglHybrisReleaseNativeBuffer;
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR;
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;
} egl_proc_rec, *egl_proc_ptr;

typedef struct {
    EGLSurface surface;
    EGLContext context;
    EGLContext renderContext;
    GLuint rootTexture;

    float projection[16];
    EGLImageKHR image;

    hwc_renderer_shader rootShader;
    hwc_renderer_shader projShader;
} hwc_renderer_rec, *hwc_renderer_ptr;

typedef struct {
	int index;
	int dpmsMode;

	int width;
	int height;
//	hwc_rotation rotation;
    hwc_rotation rotationOnFirstSetMode;

	EGLClientBuffer buffer;
	int stride;
	DamagePtr damage;
	Bool dirty;

	Bool cursorShown;
	int cursorX;
	int cursorY;

    xf86CrtcPtr pCrtc;
    xf86OutputPtr pOutput;

    struct ANativeWindow *win;
    hwc_renderer_rec hwc_renderer;
	uint32_t hwcVersion;

	//hwc v1 items
	hwc_composer_device_1_t *hwcDevicePtr;
	hwc_display_contents_1_t **hwcContents;
	hwc_layer_1_t *fblayer;

	//hwc v2 items
	hwc2_compat_display_t* hwc2_compat_display;
	hwc2_compat_layer_t* hwc2_compat_layer;
	int lastPresentFence;
} hwc_display_rec, *hwc_display_ptr;

typedef struct udev_switches_data
{
    ScrnInfoPtr pScrn;
    struct udev *udev;
    struct udev_monitor *monitor;
} udev_switches_data_rec, *udev_switches_data_ptr;

void hwc_trigger_redraw(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display);
Bool hwc_egl_renderer_tidy(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display);
Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, Bool do_glamor);
void hwc_get_native_window(hwc_display_ptr hwc_display);
void hwc_egl_renderer_update(ScreenPtr pScreen, hwc_display_ptr display);
PixmapPtr get_crtc_pixmap(hwc_display_ptr hwc_display);
void dummy_crtc_shadow_destroy(xf86CrtcPtr crtc, PixmapPtr pPixmap, void *data);
Bool hwc_display_init(ScrnInfoPtr pScrn, hwc_display_ptr display, hwc2_compat_device_t* hwc2_compat_device, int id);
void hwc_output_set_mode(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, int index, int mode);
Bool hdmi_power_enable(Bool enable);
Bool hdmi_enable(Bool enable);
Bool hdmi_set_video_config(int vformat);
void hwc_egl_renderer_external_power_up(ScrnInfoPtr pScrn);
Bool init_udev_switches(udev_switches_data_ptr data);
Bool close_udev_switches(udev_switches_data_ptr data);

typedef struct HWCRec
{
    /* options */
    OptionInfoPtr Options;
    Bool swCursor;
    /* proc pointer */
    CloseScreenProcPtr CloseScreen;
    CreateScreenResourcesProcPtr	CreateScreenResources;
    xf86CursorInfoPtr CursorInfo;
    ScreenBlockHandlerProcPtr BlockHandler;
    OsTimerPtr timer;

    dummy_colors colors[1024];
    Bool        (*CreateWindow)() ;     /* wrapped CreateWindow */
    Bool prop;

    /* XRANDR support begin */
    int num_screens;
    int connected_outputs;
    /* XRANDR support end */

	DamagePtr damage;
	Bool dirty;
    Bool glamor;
    Bool drihybris;
    Bool wasRotated;

    gralloc_module_t *gralloc;
    alloc_device_t *alloc;
    void *libminisf;

    hwc2_compat_device_t* hwc2Device;
	hwc_display_rec primary_display;
	hwc_display_rec external_display;

    udev_switches_data_rec udev_switches;
	hwc2_display_t external_display_id;

    EGLClientBuffer buffer;
    int stride;

    xf86CursorInfoPtr cursorInfo;
    int cursorWidth;
    int cursorHeight;
	GLuint cursorTexture;

    struct light_device_t *lightsDevice;
    int screenBrightness;

    DisplayModePtr modes;

	egl_proc_rec egl_proc;
	EGLDisplay egl_display;
	EGLSyncKHR egl_fence;

	pthread_t rendererThread;
    int rendererIsRunning;
    pthread_mutex_t rendererLock;
    pthread_mutex_t dirtyLock;
    pthread_cond_t dirtyCond;
} HWCRec, *HWCPtr;

/* The privates of the hwcomposer driver */
#define HWCPTR(p)	((HWCPtr)((p)->driverPrivate))

/* Return codes from all functions
typedef enum {
    HWC2_ERROR_NONE = 0,
    HWC2_ERROR_BAD_CONFIG,
    HWC2_ERROR_BAD_DISPLAY,
    HWC2_ERROR_BAD_LAYER,
    HWC2_ERROR_BAD_PARAMETER,
    HWC2_ERROR_HAS_CHANGES,
    HWC2_ERROR_NO_RESOURCES,
    HWC2_ERROR_NOT_VALIDATED,
    HWC2_ERROR_UNSUPPORTED,
} hwc2_error_t;
*/