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

void hwc_trigger_redraw(ScrnInfoPtr pScrn);
Bool hwc_display_pre_init(ScrnInfoPtr pScrn, xf86CrtcPtr *crtc, xf86OutputPtr *output);
Bool hwc_hwcomposer_init(ScrnInfoPtr pScrn);
Bool hwc_hwcomposer2_init(ScrnInfoPtr pScrn);
void hwc_hwcomposer_close(ScrnInfoPtr pScrn);
Bool hwc_lights_init(ScrnInfoPtr pScrn);
Bool hwc_drihybris_screen_init(ScreenPtr screen);

struct ANativeWindow *hwc_get_native_window(ScrnInfoPtr pScrn);
void hwc_toggle_screen_brightness(ScrnInfoPtr pScrn);
void hwc_set_power_mode(ScrnInfoPtr pScrn, int disp, int mode);
void hwc_set_power_mode_hwcomposer2(ScrnInfoPtr pScrn, int disp, int mode);

Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn);
Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn, Bool do_glamor);
void hwc_egl_renderer_close(ScrnInfoPtr pScrn);
void hwc_egl_renderer_screen_init(ScreenPtr pScreen);
void hwc_egl_renderer_screen_close(ScreenPtr pScreen);
void *hwc_egl_renderer_thread(void *user_data);
void hwc_egl_renderer_update(ScreenPtr pScreen);

void hwc_ortho_2d(float* mat, float left, float right, float bottom, float top);
GLuint hwc_link_program(const GLchar *vert_src, const GLchar *frag_src);

Bool hwc_present_screen_init(ScreenPtr pScreen);
Bool hwc_cursor_init(ScreenPtr pScreen);

typedef enum {
    HWC_ROTATE_NORMAL,
    HWC_ROTATE_CW,
    HWC_ROTATE_UD,
    HWC_ROTATE_CCW
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

    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    EGLContext renderContext;
    GLuint rootTexture;
    GLuint cursorTexture;

    float projection[16];
    EGLImageKHR image;
    EGLSyncKHR fence;

    hwc_renderer_shader rootShader;
    hwc_renderer_shader projShader;
} hwc_renderer_rec, *hwc_renderer_ptr;

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
    struct _xf86Crtc *paCrtcs[HWC_MAX_SCREENS];
    struct _xf86Output *paOutputs[HWC_MAX_SCREENS];
    int connected_outputs;
    /* XRANDR support end */

    DamagePtr damage;
    Bool dirty;
    Bool glamor;
    Bool drihybris;
    hwc_rotation rotation;

    gralloc_module_t *gralloc;
    alloc_device_t *alloc;
    void *libminisf;

    hwc_composer_device_1_t *hwcDevicePtr;
    hwc_display_contents_1_t **hwcContents;
    hwc_layer_1_t *fblayer;
    uint32_t hwcVersion;
    int hwcWidth;
    int hwcHeight;
    int lastPresentFence;

    hwc2_compat_device_t* hwc2Device;
    hwc2_compat_display_t* hwc2_primary_display;
    hwc2_compat_layer_t* hwc2_primary_layer;

	hwc2_compat_display_t* hwc2_external_display;
	hwc2_compat_layer_t* hwc2_external_layer;
	bool external_connected;
	hwc2_display_t external_display_id;
	int extWidth;
	int extHeight;

	hwc_renderer_rec renderer;
    EGLClientBuffer buffer;
    int stride;

    Bool cursorShown;
    xf86CursorInfoPtr cursorInfo;
    int cursorX;
    int cursorY;
    int cursorWidth;
    int cursorHeight;

    struct light_device_t *lightsDevice;
    int screenBrightness;

    DisplayModePtr modes;
    int dpmsMode;

    pthread_t rendererThread;
    int rendererIsRunning;
    pthread_mutex_t rendererLock;
    pthread_mutex_t dirtyLock;
    pthread_cond_t dirtyCond;
} HWCRec, *HWCPtr;

/* The privates of the hwcomposer driver */
#define HWCPTR(p)	((HWCPtr)((p)->driverPrivate))

