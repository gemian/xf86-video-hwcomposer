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

Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, Bool do_glamor)
{
    HWCPtr hwc = HWCPTR(pScrn);
    hwc_renderer_ptr renderer = &hwc_display->hwc_renderer;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init\n");

    EGLDisplay egl_display;
    EGLConfig ecfg;
    EGLint num_config;
    EGLint attr[] = {       // some attributes to set up our egl-interface
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
    EGLSurface surface;
    EGLint ctxattr[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };
    EGLContext context;
    EGLBoolean rv;
    int err;

    if (!hwc_display->win) {
        hwc_get_native_window(hwc_display);
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init got native window\n");

    if (hwc->egl_display == NULL) {
        egl_display = eglGetDisplay(NULL);
        assert(eglGetError() == EGL_SUCCESS);
        assert(egl_display != EGL_NO_DISPLAY);
        hwc->egl_display = egl_display;

        rv = eglInitialize(egl_display, 0, 0);
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);

        eglChooseConfig((EGLDisplay) egl_display, attr, &ecfg, 1, &num_config);
        assert(eglGetError() == EGL_SUCCESS);
        assert(rv == EGL_TRUE);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglGetDisplay+eglChooseConfig done\n");
    } else {
        egl_display = hwc->egl_display;
        eglChooseConfig((EGLDisplay) egl_display, attr, &ecfg, 1, &num_config);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init eglChooseConfig done\n");
    }

    if (!renderer->surface) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating window surface\n");
        surface = eglCreateWindowSurface((EGLDisplay) egl_display, ecfg, hwc_display->win, NULL);
        assert(eglGetError() == EGL_SUCCESS);
        assert(surface != EGL_NO_SURFACE);
        renderer->surface = surface;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created window surface\n");
    }

    if (!renderer->context) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating context\n");
        context = eglCreateContext((EGLDisplay) egl_display, ecfg, EGL_NO_CONTEXT, ctxattr);
        assert(eglGetError() == EGL_SUCCESS);
        assert(context != EGL_NO_CONTEXT);
        renderer->context = context;
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created context\n");
    }

    // Create shared context for render thread
    if (!renderer->renderContext) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init creating render surface\n");
        renderer->renderContext = eglCreateContext(egl_display, ecfg, context, ctxattr);
        assert(eglGetError() == EGL_SUCCESS);
        assert(renderer->renderContext != EGL_NO_CONTEXT);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init created render surface\n");
    }

    assert(eglMakeCurrent((EGLDisplay) egl_display, surface, surface, context) == EGL_TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init made current for init\n");

    // During init, enable debug output
    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(MessageCallback, 0);

    const char *version = glGetString(GL_VERSION);
    assert(version);
    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init version: %s\n", version);

    glGenTextures(1, &renderer->rootTexture);
    glGenTextures(1, &hwc->cursorTexture);
    renderer->image = EGL_NO_IMAGE_KHR;
    renderer->rootShader.program = 0;
    renderer->projShader.program = 0;
    hwc->egl_fence = EGL_NO_SYNC_KHR;

    // Reattach context as surfaceless so it can be used in different thread
    assert(eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, context) == EGL_TRUE);

    return TRUE;
}

void triggerGlamourEglFlush(HWCPtr hwc);

void hwc_egl_renderer_screen_init(ScreenPtr pScreen, int disp)
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

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init\n");

    int result = eglMakeCurrent(hwc->egl_display, renderer->surface, renderer->surface, renderer->renderContext);
    printf("%d %d\n", result, eglGetError());
    assert(result == EGL_TRUE);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - made current\n");

    glBindTexture(GL_TEXTURE_2D, renderer->rootTexture);

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - bound root texture\n");

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!hwc->glamor && renderer->image == EGL_NO_IMAGE_KHR) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create image\n");
        renderer->image = hwc->egl_proc.eglCreateImageKHR(hwc->egl_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_HYBRIS,
                                                          (EGLClientBuffer) display->buffer, NULL);
        hwc->egl_proc.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, renderer->image);
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created image\n");
    }

    if (!renderer->rootShader.program) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create rootShader program\n");
        GLuint prog;
        renderer->rootShader.program = prog =
            hwc_link_program(vertex_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link root window shader\n");
        }

        renderer->rootShader.position = glGetAttribLocation(prog, "position");
        renderer->rootShader.texcoords = glGetAttribLocation(prog, "texcoords");
        renderer->rootShader.transform = glGetUniformLocation(prog, "transform");
        renderer->rootShader.texture = glGetUniformLocation(prog, "texture");
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created rootShader program\n");
    }

    if (!renderer->projShader.program) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - create projShader program\n");
        GLuint prog;
        renderer->projShader.program = prog =
            hwc_link_program(vertex_mvp_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link cursor shader\n");
        }

        renderer->projShader.position = glGetAttribLocation(prog, "position");
        renderer->projShader.texcoords = glGetAttribLocation(prog, "texcoords");
        renderer->projShader.transform = glGetUniformLocation(prog, "transform");
        renderer->projShader.texture = glGetUniformLocation(prog, "texture");
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init - created projShader program\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_screen_init out\n");
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

    glUseProgram(renderer->projShader.program);

    glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
    glUniform1i(renderer->projShader.texture, 0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    hwc_translate_cursor(display->pCrtc->rotation, display->cursorX, display->cursorY,
                         hwc->cursorWidth, hwc->cursorHeight,
                         display->width, display->height,
                         cursorVertices);

    glVertexAttribPointer(renderer->projShader.position, 2, GL_FLOAT, 0, 0, cursorVertices);
    glEnableVertexAttribArray(renderer->projShader.position);

    glVertexAttribPointer(renderer->projShader.texcoords, 2, GL_FLOAT, 0, 0,
                          getTextureVerticesForRotation(display->pCrtc->rotation));
    glEnableVertexAttribArray(renderer->projShader.texcoords);

    glUniformMatrix4fv(renderer->projShader.transform, 1, GL_FALSE, renderer->projection);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(renderer->projShader.position);
    glDisableVertexAttribArray(renderer->projShader.texcoords);
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

void *hwc_egl_renderer_thread(void *user_data)
{
    ScreenPtr pScreen = (ScreenPtr) user_data;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    bool external_initialised = FALSE;
    bool primary_dirty = FALSE;
    bool external_dirty = FALSE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread\n");

    hwc_egl_renderer_screen_init(pScreen, 0);
    hwc->wasRotated = TRUE;

    while (hwc->rendererIsRunning) {
        pthread_mutex_lock(&(hwc->dirtyLock));

        while (!(hwc->primary_display.dirty || hwc->external_display.dirty)) {
            pthread_cond_wait(&(hwc->dirtyCond), &(hwc->dirtyLock));
        }

        primary_dirty = hwc->primary_display.dirty;
        external_dirty = hwc->external_display.dirty;
        hwc->primary_display.dirty = FALSE;
        hwc->external_display.dirty = FALSE;

        pthread_mutex_unlock(&(hwc->dirtyLock));

        if (hwc->wasRotated) {
            updateProjection(pScrn, 0);
            hwc->wasRotated = FALSE;
        }

        if ((hwc->connected_outputs & 2) && !external_initialised) {
            hwc_display_init(pScrn, &hwc->external_display, hwc->hwc2Device, hwc->external_display_id);
            Bool ret = RRGetInfo(pScreen, TRUE);
            xf86DrvMsg(pScrn->scrnIndex, X_INFO, "RRGetInfo ret:%d\n", ret);
            hwc_egl_renderer_screen_init(pScreen, 1);
            hwc_output_set_mode(pScrn, &hwc->external_display, 1, DPMSModeOn);
            updateProjection(pScrn, 1);
            external_initialised = TRUE;
            external_dirty = TRUE;
        }
        if (!(hwc->connected_outputs & 2) && external_initialised) {
            external_initialised = FALSE;
        }

        pthread_mutex_lock(&(hwc->rendererLock));
        if (hwc->egl_fence != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(hwc->egl_display,
                                 hwc->egl_fence,
                                 EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                 EGL_FOREVER_KHR);
        }
        if (primary_dirty && hwc->primary_display.dpmsMode == DPMSModeOn)
            hwc_egl_renderer_update(pScreen, &hwc->primary_display);
        if (external_dirty && hwc->external_display.dpmsMode == DPMSModeOn && external_initialised)
            hwc_egl_renderer_update(pScreen, &hwc->external_display);
        pthread_mutex_unlock(&(hwc->rendererLock));
    }

    if (external_initialised) {
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

    assert(eglMakeCurrent(hwc->egl_display, renderer->surface, renderer->surface, renderer->renderContext) == EGL_TRUE);

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

    glUseProgram(renderer->rootShader.program);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, renderer->rootTexture);
    glUniform1i(renderer->rootShader.texture, 0);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 2\n");

    glVertexAttribPointer(renderer->rootShader.position, 2, GL_FLOAT, 0, 0, squareVertices);
    glEnableVertexAttribArray(renderer->rootShader.position);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 3\n");

    glVertexAttribPointer(renderer->rootShader.texcoords, 2, GL_FLOAT, 0, 0,
                          getTextureVerticesForRotation(display->pCrtc->rotation));
    glEnableVertexAttribArray(renderer->rootShader.texcoords);

//    glUniformMatrix4fv(renderer->rootShader.transform, 1, GL_FALSE, renderer->projection);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 4\n");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 5\n");

    glDisableVertexAttribArray(renderer->rootShader.position);
    glDisableVertexAttribArray(renderer->rootShader.texcoords);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 6\n");

    if (display->cursorShown)
        hwc_egl_render_cursor(pScreen, display);

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
