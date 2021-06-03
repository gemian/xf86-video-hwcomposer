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
#include <xf86RandR12.h>

#include "driver.h"

extern const char vertex_src[];
extern const char vertex_mvp_src[];
extern const char fragment_src[];
extern const char fragment_src_bgra[];

static const GLfloat squareVertices[] = {
    -1.0f, -1.0f,
    1.0f, -1.0f,
    -1.0f, 1.0f,
    1.0f, 1.0f,
};

static const GLfloat textureVertices[][8] = {
    { // NORMAL - 0 degrees
        0.0f, 1.0f,
        1.0f, 1.0f,
        0.0f, 0.0f,
        1.0f, 0.0f,
    },
    { // CW - 90 degrees
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        0.0f, 0.0f,
    },
    { // UD - 180 degrees
        1.0f, 0.0f,
        0.0f, 0.0f,
        1.0f, 1.0f,
        0.0f, 1.0f,
    },
    { // CCW - 270 degrees
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    }
};

const EGLint egl_attr[] = {       // some attributes to set up our egl-interface
    EGL_RED_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_BLUE_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_DEPTH_SIZE, 24,
    EGL_STENCIL_SIZE, 8,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
    EGL_NONE
};
static const EGLint egl_ctxattr[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

GLfloat cursorVertices[8];

const GLfloat *getTextureVerticesForRotation(hwc_rotation rotation)
{
    switch (rotation) {
        case HWC_ROTATE_NORMAL:
            return textureVertices[0];
        case HWC_ROTATE_CW:
            return textureVertices[1];
        case HWC_ROTATE_UD:
            return textureVertices[2];
        case HWC_ROTATE_CCW:
            return textureVertices[3];
    }
}

Bool hwc_init_hybris_native_buffer(ScrnInfoPtr pScrn)
{
    HWCPtr hwc = HWCPTR(pScrn);

    if (strstr(eglQueryString(hwc->egl_display, EGL_EXTENSIONS), "EGL_HYBRIS_native_buffer") == NULL)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "EGL_HYBRIS_native_buffer is missing. Make sure libhybris EGL implementation is used\n");
        return FALSE;
    }

    hwc->egl_proc.eglHybrisCreateNativeBuffer = (PFNEGLHYBRISCREATENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisCreateNativeBuffer");
    assert(hwc->egl_proc.eglHybrisCreateNativeBuffer != NULL);

    hwc->egl_proc.eglHybrisCreateRemoteBuffer = (PFNEGLHYBRISCREATEREMOTEBUFFERPROC) eglGetProcAddress("eglHybrisCreateRemoteBuffer");
    assert(hwc->egl_proc.eglHybrisCreateRemoteBuffer != NULL);

    hwc->egl_proc.eglHybrisLockNativeBuffer = (PFNEGLHYBRISLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisLockNativeBuffer");
    assert(hwc->egl_proc.eglHybrisLockNativeBuffer != NULL);

    hwc->egl_proc.eglHybrisUnlockNativeBuffer = (PFNEGLHYBRISUNLOCKNATIVEBUFFERPROC) eglGetProcAddress("eglHybrisUnlockNativeBuffer");
    assert(hwc->egl_proc.eglHybrisUnlockNativeBuffer != NULL);

    hwc->egl_proc.eglHybrisReleaseNativeBuffer = (PFNEGLHYBRISRELEASENATIVEBUFFERPROC) eglGetProcAddress("eglHybrisReleaseNativeBuffer");
    assert(hwc->egl_proc.eglHybrisReleaseNativeBuffer != NULL);

    hwc->egl_proc.eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    assert(hwc->egl_proc.eglCreateImageKHR != NULL);

    hwc->egl_proc.eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
    assert(hwc->egl_proc.eglDestroyImageKHR != NULL);

	hwc->egl_proc.glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    assert(hwc->egl_proc.glEGLImageTargetTexture2DOES != NULL);
    return TRUE;
}

void GLAPIENTRY
MessageCallback( GLenum source,
                 GLenum type,
                 GLuint id,
                 GLenum severity,
                 GLsizei length,
                 const GLchar* message,
                 const void* userParam )
{
    fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
}

Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn, Bool do_glamor)
{
    HWCPtr hwc = HWCPTR(pScrn);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init\n");

    EGLint num_config;
    EGLBoolean rv;
    int err;

    if (!hwc->primary_display.win) {
        hwc_get_native_window(hwc, &hwc->primary_display);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init got native window\n");

    if (hwc->egl_display == NULL) {
        hwc->egl_display = eglGetDisplay(NULL);
        if (eglGetError() != EGL_SUCCESS || hwc->egl_display == EGL_NO_DISPLAY) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglGetDisplay eglGetError: %d, egl_display: %p\n", eglGetError(), hwc->egl_display);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc->egl_display != EGL_NO_DISPLAY);

        rv = eglInitialize(hwc->egl_display, 0, 0);
        if (eglGetError() != EGL_SUCCESS || rv != EGL_TRUE) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglInitialize eglGetError: %d, rv: %d\n", eglGetError(), rv);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);

        rv = eglChooseConfig((EGLDisplay) hwc->egl_display, egl_attr, &hwc->egl_cfg, 1, &num_config);
        if (eglGetError() != EGL_SUCCESS || rv != EGL_TRUE) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglChooseConfig eglGetError: %d, rv: %d\n", eglGetError(), rv);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglGetDisplay+eglChooseConfig done\n");
    } else {
        rv = eglChooseConfig((EGLDisplay) hwc->egl_display, egl_attr, &hwc->egl_cfg, 1, &num_config);
        if (eglGetError() != EGL_SUCCESS || rv != EGL_TRUE) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglChooseConfig eglGetError: %d, rv: %d\n", eglGetError(), rv);
        }
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglChooseConfig done\n");
    }

    if (!hwc->primary_display.hwc_renderer.surface) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating primary window surface\n");
        hwc->primary_display.hwc_renderer.surface = eglCreateWindowSurface((EGLDisplay) hwc->egl_display, hwc->egl_cfg, (EGLNativeWindowType) hwc->primary_display.win, NULL);
        if (eglGetError() != EGL_SUCCESS || hwc->primary_display.hwc_renderer.surface == EGL_NO_SURFACE) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglCreateWindowSurface eglGetError: %d, primary surface: %p\n", eglGetError(), hwc->primary_display.hwc_renderer.surface);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc->primary_display.hwc_renderer.surface != EGL_NO_SURFACE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created primary window surface\n");
    }

    if (!hwc->primary_display.hwc_renderer.context) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating primary context\n");
        hwc->primary_display.hwc_renderer.context = eglCreateContext((EGLDisplay) hwc->egl_display, hwc->egl_cfg, EGL_NO_CONTEXT, egl_ctxattr);
        if (eglGetError() != EGL_SUCCESS || hwc->primary_display.hwc_renderer.context == EGL_NO_CONTEXT) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglCreateWindowSurface eglGetError: %d, primary context: %p\n", eglGetError(), hwc->primary_display.hwc_renderer.context);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc->primary_display.hwc_renderer.context != EGL_NO_CONTEXT);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created primary context\n");
    }

    // Create shared context for render thread
    if (!hwc->primary_display.hwc_renderer.renderContext) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating render primary context\n");
        hwc->primary_display.hwc_renderer.renderContext = eglCreateContext(hwc->egl_display, hwc->egl_cfg, hwc->primary_display.hwc_renderer.context, egl_ctxattr);
        if (eglGetError() != EGL_SUCCESS || hwc->primary_display.hwc_renderer.renderContext == EGL_NO_CONTEXT) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglCreateWindowSurface eglGetError: %d, primary surface: %p\n", eglGetError(), hwc->primary_display.hwc_renderer.renderContext);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(hwc->primary_display.hwc_renderer.renderContext != EGL_NO_CONTEXT);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created render primary context\n");
    }
    rv = eglMakeCurrent((EGLDisplay) hwc->egl_display, hwc->primary_display.hwc_renderer.surface, hwc->primary_display.hwc_renderer.surface, hwc->primary_display.hwc_renderer.context);
    if (eglGetError() != EGL_SUCCESS || rv != EGL_TRUE) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglMakeCurrent primarySurface eglGetError: %d, rv: %d\n", eglGetError(), rv);
    }
    assert(rv == EGL_TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init made current for primary init\n");

    // During init, enable debug output
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);

    const char *version = glGetString(GL_VERSION);
    if (version == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init version: %s\n", version);
    }
    assert(version);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init version: %s\n", version);

    glGenTextures(1, &hwc->rootTexture);
    glGenTextures(1, &hwc->cursorTexture);

    // Reattach context as surfaceless so it can be used in different thread
    rv = eglMakeCurrent(hwc->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, hwc->primary_display.hwc_renderer.context);
    if (eglGetError() != EGL_SUCCESS || rv != EGL_TRUE) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglMakeCurrent NoSurface eglGetError: %d, rv: %d\n", eglGetError(), rv);
    }
    assert(rv == EGL_TRUE);

    hwc->primary_display.hwc_renderer.image = EGL_NO_IMAGE_KHR;
    hwc->external_display.hwc_renderer.image = EGL_NO_IMAGE_KHR;
    hwc->rootShader.program = 0;
    hwc->projShader.program = 0;
    hwc->egl_fence = EGL_NO_SYNC_KHR;

    return TRUE;
}

void triggerGlamourEglFlush(HWCPtr hwc);

void hwc_egl_renderer_screen_init(ScreenPtr pScreen, int disp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_display_ptr hwc_display = NULL;
    if (disp == HWC_DISPLAY_PRIMARY) {
        hwc_display = &hwc->primary_display;
    } else {
        hwc_display = &hwc->external_display;
    }
    hwc_renderer_ptr renderer = &hwc_display->hwc_renderer;
    EGLint num_config;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init win: %p\n",hwc_display->win);

    if (!hwc_display->win) {
        if (hwc_display->width == 0) {
            hwc_display->width = 1920;
            hwc_display->height = 1080;
        }
        hwc_get_native_window(hwc, hwc_display);
    }

//    eglChooseConfig((EGLDisplay) hwc->egl_display, egl_attr, &hwc->egl_cfg, 1, &num_config);

    if (!renderer->surface) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init creating external window surface\n");
        renderer->surface = eglCreateWindowSurface((EGLDisplay) hwc->egl_display, hwc->egl_cfg, (EGLNativeWindowType) hwc_display->win, NULL);
        if (eglGetError() != EGL_SUCCESS || renderer->surface == EGL_NO_SURFACE) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init eglCreateWindowSurface eglError: %d, surface: %p\n",eglGetError(),renderer->surface);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(renderer->surface != EGL_NO_SURFACE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init created external window surface\n");
    }

    if (!renderer->context) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init creating external context\n");
        renderer->context = eglCreateContext((EGLDisplay) hwc->egl_display, hwc->egl_cfg, EGL_NO_CONTEXT, egl_ctxattr);
        if (eglGetError() != EGL_SUCCESS || renderer->context == EGL_NO_CONTEXT) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init eglCreateContext eglError: %d, context: %p\n",eglGetError(),renderer->context);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(renderer->context != EGL_NO_CONTEXT);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init created external context\n");
    }

    if (!renderer->renderContext) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init creating render external surface\n");
        renderer->renderContext = eglCreateContext(hwc->egl_display, hwc->egl_cfg, renderer->context, egl_ctxattr);
        if (eglGetError() != EGL_SUCCESS || renderer->renderContext == EGL_NO_CONTEXT) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init eglCreateContext eglError: %d, renderContext: %p\n",eglGetError(),renderer->renderContext);
        }
        assert(eglGetError() == EGL_SUCCESS);
        assert(renderer->renderContext != EGL_NO_CONTEXT);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init created render external surface\n");
    }

    int result = eglMakeCurrent(hwc->egl_display, renderer->surface, renderer->surface, renderer->renderContext);
    if (eglGetError() != EGL_SUCCESS || result != EGL_TRUE) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init eglMakeCurrent: %d %d\n", eglGetError(), result);
    }
    assert(result == EGL_TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - made current\n");

    glBindTexture(GL_TEXTURE_2D, hwc->rootTexture);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - bound root texture\n");

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!hwc->glamor && renderer->image == EGL_NO_IMAGE_KHR) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create image\n");
        renderer->image = hwc->egl_proc.eglCreateImageKHR(hwc->egl_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_HYBRIS,
                                                          (EGLClientBuffer) hwc->buffer, NULL);
        hwc->egl_proc.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, renderer->image);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created image\n");
    }

    if (!hwc->rootShader.program) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create rootShader program\n");
        GLuint prog;
        hwc->rootShader.program = prog =
            hwc_link_program(vertex_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link root window shader\n");
        }

        hwc->rootShader.position = glGetAttribLocation(prog, "position");
        hwc->rootShader.texcoords = glGetAttribLocation(prog, "texcoords");
        //hwc->rootShader.transform = glGetUniformLocation(prog, "transform");
        hwc->rootShader.texture = glGetUniformLocation(prog, "texture");
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created rootShader program\n");
    }

    if (!hwc->projShader.program) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create projShader program\n");
        GLuint prog;
        hwc->projShader.program = prog =
            hwc_link_program(vertex_mvp_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link cursor shader\n");
        }

        hwc->projShader.position = glGetAttribLocation(prog, "position");
        hwc->projShader.texcoords = glGetAttribLocation(prog, "texcoords");
        hwc->projShader.transform = glGetUniformLocation(prog, "transform");
        hwc->projShader.texture = glGetUniformLocation(prog, "texture");
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created projShader program\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init out\n");
}

void hwc_egl_renderer_external_power_up(ScrnInfoPtr pScrn)
{
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_external_power_up\n");
    hdmi_power_enable(TRUE);
    hdmi_enable(TRUE);
    hdmi_set_video_config(0);
}

void hwc_egl_renderer_external_tidy(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_renderer_ptr renderer = &hwc->external_display.hwc_renderer;

    int ret = eglDestroySurface(hwc->egl_display, renderer->surface);
    renderer->surface = NULL;
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_external_tidy destroySurface ret: %d\n", ret);

    hwc2_compat_display_destroy_layer(hwc->external_display.hwc2_compat_display, hwc->external_display.hwc2_compat_layer);
    hwc->external_display.hwc2_compat_layer = NULL;
    hwc2_compat_display_set_power_mode(hwc->external_display.hwc2_compat_display, HWC2_POWER_MODE_OFF);

    hdmi_power_enable(FALSE);
    hdmi_enable(FALSE);
}

void hwc_translate_cursor(hwc_rotation rotation, int x, int y, int cursorWidth, int cursorHeight,
                          int displayWidth, int displayHeight,
                          float *vertices)
{
    int t;
    int i = 0;

    xf86DrvMsg(0, X_INFO, "hwc_translate_cursor x: %d, y: %d, width: %d, height: %d, displayW: %d, displayH: %d\n",
               x, y, cursorWidth, cursorHeight, displayWidth, displayHeight);

    #define P(x, y) vertices[i++] = x;  vertices[i++] = y; // Point vertex
    switch (rotation) {
        case HWC_ROTATE_NORMAL:
            y = displayHeight - y;
            P(x, y - cursorHeight);
            P(x + cursorWidth, y - cursorHeight);
            P(x, y);
            P(x + cursorWidth, y);
            break;
        case HWC_ROTATE_CW:
            t = x;
            x = displayHeight - y * 2;
            y = displayWidth - t / 2;
            cursorWidth *= 2;
            cursorHeight /= 2;

            P(x - cursorWidth, y - cursorHeight);
            P(x, y - cursorHeight);
            P(x - cursorWidth, y);
            P(x, y);
            break;
        case HWC_ROTATE_UD:
            x = displayWidth - x;

            P(x - cursorWidth, y);
            P(x, y);
            P(x - cursorWidth, y + cursorHeight);
            P(x, y + cursorHeight);
            break;
        case HWC_ROTATE_CCW:
            t = x;
            x = y * 2;
            y = t / 2;
            cursorWidth *= 2;
            cursorHeight /= 2;
            P(x, y);
            P(x + cursorWidth, y);
            P(x, y + cursorHeight);
            P(x + cursorWidth, y + cursorHeight);
            break;
    }
    #undef P
}

void hwc_egl_render_cursor(ScreenPtr pScreen, hwc_display_ptr display)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_renderer_ptr renderer = &display->hwc_renderer;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_render_cursor disp: %p, rotation: %d, xDpi: %d, yDpi: %d\n",
	           display, display->pCrtc->rotation, pScrn->xDpi, pScrn->yDpi);

    glUseProgram(hwc->projShader.program);

    glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
    glUniform1i(hwc->projShader.texture, 0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    hwc_translate_cursor(display->pCrtc->rotation, display->cursorX, display->cursorY,
                         hwc->cursorWidth, hwc->cursorHeight,
                         display->width, display->height,
                         cursorVertices);

    glVertexAttribPointer(hwc->projShader.position, 2, GL_FLOAT, 0, 0, cursorVertices);
    glEnableVertexAttribArray(hwc->projShader.position);

    glVertexAttribPointer(hwc->projShader.texcoords, 2, GL_FLOAT, 0, 0,
                          getTextureVerticesForRotation(display->pCrtc->rotation));
    glEnableVertexAttribArray(hwc->projShader.texcoords);

    glUniformMatrix4fv(hwc->projShader.transform, 1, GL_FALSE, renderer->projection);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(hwc->projShader.position);
    glDisableVertexAttribArray(hwc->projShader.texcoords);
}

void updateProjection(ScrnInfoPtr pScrn, int disp)
{
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_display_ptr display = NULL;
    if (disp == HWC_DISPLAY_PRIMARY) {
        display = &hwc->primary_display;
    } else {
        display = &hwc->external_display;
    }
    hwc_renderer_ptr renderer = &display->hwc_renderer;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "updateProjection disp: %d, rotation: %d\n", disp, display->pCrtc->rotation);

    if (display->pCrtc->rotation == HWC_ROTATE_CW || display->pCrtc->rotation == HWC_ROTATE_CCW)
        hwc_ortho_2d(renderer->projection, 0.0f, display->height, 0.0f, display->width);
    else
        hwc_ortho_2d(renderer->projection, 0.0f, display->width, 0.0f, display->height);
}

DisplayModePtr hwc_output_get_modes(xf86OutputPtr output);

void *hwc_egl_renderer_thread(void *user_data)
{
    ScreenPtr pScreen = (ScreenPtr) user_data;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    bool external_ever_initialised = FALSE;
    bool primary_dirty = FALSE;
    bool external_dirty = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread\n");

    hwc_egl_renderer_screen_init(pScreen, 0);
    hwc->wasRotated = TRUE;

    while (hwc->rendererIsRunning) {
        pthread_mutex_lock(&(hwc->dirtyLock));

        while (!hwc->dirty) {
            pthread_cond_wait(&(hwc->dirtyCond), &(hwc->dirtyLock));
        }

        primary_dirty = hwc->dirty;
        hwc->dirty = FALSE;

        pthread_mutex_unlock(&(hwc->dirtyLock));

        if (hwc->wasRotated) {
            updateProjection(pScrn, HWC_DISPLAY_PRIMARY);
            if (hwc->external_initialised) {
                updateProjection(pScrn, HWC_DISPLAY_EXTERNAL);
            }
            hwc->wasRotated = FALSE;
        }

        if ((hwc->connected_outputs & 2) && !hwc->external_initialised) {
            hwc_display_init(pScrn, &hwc->external_display, hwc->hwc2Device, hwc->external_display_id);
            Bool ret = xf86InitialConfiguration(pScrn, TRUE);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread xf86InitialConfiguration ret:%d, cOutput: %d\n", ret, hwc->connected_outputs);
            xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
            xf86OutputPtr pOutputPtr = config->output[0];
            DisplayModePtr pMode = hwc_output_get_modes(pOutputPtr);
            xf86OutputPtr eOutputPtr = config->output[1];
            DisplayModePtr eMode = hwc_output_get_modes(eOutputPtr);

            xf86CrtcSetMode(hwc->primary_display.pCrtc, pMode, pOutputPtr->initial_rotation, pOutputPtr->crtc->desiredX, pOutputPtr->crtc->desiredY);
            xf86CrtcSetMode(hwc->external_display.pCrtc, eMode, eOutputPtr->initial_rotation, eOutputPtr->crtc->desiredX, eOutputPtr->crtc->desiredY);

            int width = pOutputPtr->initial_x + xf86ModeWidth(pMode, pOutputPtr->initial_rotation);
            int height = pOutputPtr->initial_y + xf86ModeHeight(pMode, pOutputPtr->initial_rotation);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread width: %d, height: %d\n", width, height);
            int modeWidth = xf86ModeWidth(eMode, eOutputPtr->initial_rotation);
            int modeHeight = xf86ModeHeight(eMode, eOutputPtr->initial_rotation);
            if ((eOutputPtr->initial_x + modeWidth) > width) {
                width = eOutputPtr->initial_x + modeWidth;
            }
            if ((eOutputPtr->initial_y + modeHeight) > height) {
                height = eOutputPtr->initial_y + modeHeight;
            }
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread width: %d, height: %d, modeWidth: %d, modeHeight: %d, eIRot: %d\n", width, height, modeWidth, modeHeight, eOutputPtr->initial_rotation);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread RRScreenSizeSet e initial_x: %d, desiredX: %d\n", eOutputPtr->initial_x, eOutputPtr->crtc->desiredX);
            int mmWidth = width * 25.4 / pScrn->xDpi;
            int mmHeight = height * 25.4 / pScrn->yDpi;
            ret = RRScreenSizeSet(pScreen, width, height, mmWidth, mmHeight);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread RRScreenSizeSet width: %d, height: %d, ret:%d\n", width, height, ret);

            hwc_egl_renderer_screen_init(pScreen, 1);
            hwc_set_power_mode(pScrn, HWC_DISPLAY_EXTERNAL, DPMSModeOn);

            updateProjection(pScrn, HWC_DISPLAY_EXTERNAL);
            hwc->external_initialised = TRUE;
            external_ever_initialised = TRUE;
//            external_dirty = FALSE;
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread updated projection\n");
        }
        if (!(hwc->connected_outputs & 2) && hwc->external_initialised) {
            hwc->external_initialised = FALSE;
            hwc_egl_renderer_external_tidy(pScreen);
            Bool ret = xf86InitialConfiguration(pScrn, TRUE);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread xf86InitialConfiguration ret:%d, cOutput: %d\n", ret, hwc->connected_outputs);
            xf86OutputPtr outputPtr = hwc->primary_display.pOutput;
//            xf86CrtcPtr crtcPtr = hwc->primary_display.pCrtc;
//            int width = crtcPtr->x + xf86ModeWidth(&crtcPtr->mode, crtcPtr->rotation);
//            int height = crtcPtr->y + xf86ModeHeight(&crtcPtr->mode, crtcPtr->rotation);
            int width = outputPtr->initial_x + xf86ModeWidth(&outputPtr->crtc->mode, outputPtr->initial_rotation);
            int height = outputPtr->initial_y + xf86ModeHeight(&outputPtr->crtc->mode, outputPtr->initial_rotation);
            int mmWidth = width * 25.4 / pScrn->xDpi;
            int mmHeight = height * 25.4 / pScrn->yDpi;
            ret = RRScreenSizeSet(pScreen, width, height, mmWidth, mmHeight);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread external disconnected RRScreenSizeSet width: %d, height: %d, ret:%d\n", width, height, ret);
        }

        pthread_mutex_lock(&(hwc->rendererLock));
        if (hwc->egl_fence != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(hwc->egl_display,
                                 hwc->egl_fence,
                                 EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                 EGL_FOREVER_KHR);
        }
        if (primary_dirty && hwc->primary_display.dpmsMode == DPMSModeOn) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread pDisp: %p\n", &hwc->primary_display);
            hwc_egl_renderer_update(pScreen, &hwc->primary_display);
        }
        if (external_dirty && hwc->external_display.dpmsMode == DPMSModeOn && (hwc->connected_outputs & 2) && hwc->external_initialised) {
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread eDisp: %p\n", &hwc->external_display);
            hwc_egl_renderer_update(pScreen, &hwc->external_display);
        }
        pthread_mutex_unlock(&(hwc->rendererLock));
    }

    if (hwc->external_initialised) {
        hwc_egl_renderer_external_tidy(pScreen);
    }
    if (external_ever_initialised) {
        hwc_egl_renderer_screen_close(pScreen, 1);
    }
    hwc_egl_renderer_screen_close(pScreen, 0);

    return NULL;
}

void hwc_egl_renderer_update(ScreenPtr pScreen, hwc_display_ptr display)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_renderer_ptr renderer = &display->hwc_renderer;

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update surface: %p, context: %p\n", renderer->surface, renderer->context);

    int ret = eglMakeCurrent(hwc->egl_display, renderer->surface, renderer->surface, renderer->renderContext);
    if (ret != EGL_TRUE) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update surface: %p, context: %p\n", renderer->surface, renderer->context);
    }
    assert(ret == EGL_TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update glamor: %d, viewport width: %d, height: %d\n",
               hwc->glamor, display->width, display->height);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update screen w: %d, h: %d, cBx2: %d, cBy2: %d\n",
               pScreen->width, pScreen->height, display->pCrtc->bounds.x2, display->pCrtc->bounds.y2);

    if (hwc->glamor) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, display->width, display->height);
//        glViewport(0, 0, xf86ModeWidth(&display->pCrtc->mode, display->pCrtc->rotation),
//                   xf86ModeHeight(&display->pCrtc->mode, display->pCrtc->rotation));
    }

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 1\n");

    glUseProgram(hwc->rootShader.program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hwc->rootTexture);
    glUniform1i(hwc->rootShader.texture, 0);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 2\n");

    glVertexAttribPointer(hwc->rootShader.position, 2, GL_FLOAT, GL_FALSE, 0, squareVertices);
    glEnableVertexAttribArray(hwc->rootShader.position);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 3\n");

    glVertexAttribPointer(hwc->rootShader.texcoords, 2, GL_FLOAT, GL_FALSE, 0,
                          getTextureVerticesForRotation(display->pCrtc->rotation));
    glEnableVertexAttribArray(hwc->rootShader.texcoords);

//    glUniformMatrix4fv(renderer->rootShader.transform, 1, GL_FALSE, renderer->projection);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 4\n");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 5\n");

    glDisableVertexAttribArray(hwc->rootShader.position);
    glDisableVertexAttribArray(hwc->rootShader.texcoords);

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 6\n");

    if (display->cursorShown)
        hwc_egl_render_cursor(pScreen, display);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 7\n");

    eglSwapBuffers(hwc->egl_display, renderer->surface);  // get the rendered buffer to the screen
}

void hwc_egl_renderer_screen_close(ScreenPtr pScreen, int disp)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_display_ptr display = NULL;
    if (disp == HWC_DISPLAY_PRIMARY) {
        display = &hwc->primary_display;
    } else {
        display = &hwc->external_display;
    }
    hwc_renderer_ptr renderer = &display->hwc_renderer;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_close\n");

    if (renderer->image != EGL_NO_IMAGE_KHR) {
        hwc->egl_proc.eglDestroyImageKHR(hwc->egl_display, renderer->image);
        renderer->image = EGL_NO_IMAGE_KHR;
    }
}

void hwc_egl_renderer_close(ScrnInfoPtr pScrn)
{
}
