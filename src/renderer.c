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
    -1.0f,  1.0f,
    1.0f,  1.0f,
};

static const GLfloat textureVertices[][8] = {
    { // NORMAL - 0 degrees
        0.0f,  1.0f,
        1.0f, 1.0f,
        0.0f,  0.0f,
        1.0f, 0.0f,
    },
    { // CW - 90 degrees
        1.0f, 1.0f,
        1.0f, 0.0f,
        0.0f,  1.0f,
        0.0f,  0.0f,
    },
    { // UD - 180 degrees
        1.0f, 0.0f,
        0.0f,  0.0f,
        1.0f, 1.0f,
        0.0f,  1.0f,
    },
    { // CCW - 270 degrees
        0.0f,  0.0f,
        0.0f,  1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    }
};

GLfloat cursorVertices[8];

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
    assert(hwc->egl_proc.eglDestroyImageKHR  != NULL);

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
  fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
           ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message );
}

Bool hwc_egl_renderer_init(ScrnInfoPtr pScrn, hwc_display_ptr hwc_display, Bool do_glamor)
{
    HWCPtr hwc = HWCPTR(pScrn);
	hwc_renderer_ptr renderer = &hwc_display->hwc_renderer;

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

    struct ANativeWindow *win = hwc_get_native_window(hwc_display);

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
	} else {
		egl_display = hwc->egl_display;
    }

    surface = eglCreateWindowSurface((EGLDisplay) egl_display, ecfg, (EGLNativeWindowType)win, NULL);
    assert(eglGetError() == EGL_SUCCESS);
    assert(surface != EGL_NO_SURFACE);
    renderer->surface = surface;

    context = eglCreateContext((EGLDisplay) egl_display, ecfg, EGL_NO_CONTEXT, ctxattr);
    assert(eglGetError() == EGL_SUCCESS);
    assert(context != EGL_NO_CONTEXT);
    renderer->context = context;

    // Create shared context for render thread
    renderer->renderContext = eglCreateContext(egl_display, ecfg, context, ctxattr);
    assert(eglGetError() == EGL_SUCCESS);
    assert(renderer->renderContext != EGL_NO_CONTEXT);

    assert(eglMakeCurrent((EGLDisplay) egl_display, surface, surface, context) == EGL_TRUE);

    // During init, enable debug output
    glEnable              ( GL_DEBUG_OUTPUT );
    glDebugMessageCallback( MessageCallback, 0 );


    const char *version = glGetString(GL_VERSION);
    assert(version);
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_init version: %s\n",version);

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

    glBindTexture(GL_TEXTURE_2D, renderer->rootTexture);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (!hwc->glamor && renderer->image == EGL_NO_IMAGE_KHR) {
        renderer->image = hwc->egl_proc.eglCreateImageKHR(hwc->egl_display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_HYBRIS,
                                            (EGLClientBuffer)hwc->buffer, NULL);
		hwc->egl_proc.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, renderer->image);
    }

    if (!renderer->rootShader.program) {
        GLuint prog;
        renderer->rootShader.program = prog =
            hwc_link_program(vertex_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link root window shader\n");
        }

        renderer->rootShader.position  = glGetAttribLocation(prog, "position");
        renderer->rootShader.texcoords = glGetAttribLocation(prog, "texcoords");
        renderer->rootShader.texture = glGetUniformLocation(prog, "texture");
    }

    if (!renderer->projShader.program) {
        GLuint prog;
        renderer->projShader.program = prog =
            hwc_link_program(vertex_mvp_src, (hwc->glamor || hwc->drihybris) ? fragment_src : fragment_src_bgra);

        if (!prog) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "hwc_egl_renderer_screen_init: failed to link cursor shader\n");
        }

        renderer->projShader.position  = glGetAttribLocation(prog, "position");
        renderer->projShader.texcoords = glGetAttribLocation(prog, "texcoords");
        renderer->projShader.transform = glGetUniformLocation(prog, "transform");
        renderer->projShader.texture = glGetUniformLocation(prog, "texture");
    }

    if (display->rotation == HWC_ROTATE_CW || display->rotation == HWC_ROTATE_CCW)
        hwc_ortho_2d(renderer->projection, 0.0f, display->height, 0.0f, display->width);
    else
        hwc_ortho_2d(renderer->projection, 0.0f, display->width, 0.0f, display->height);
}

void hwc_translate_cursor(hwc_rotation rotation, int x, int y, int width, int height,
                          int displayWidth, int displayHeight,
                          float* vertices) {
    int w = displayWidth, h = displayHeight;
    int cw = width, ch = height;
    int t;
    int i = 0;

	xf86DrvMsg(0, X_INFO, "hwc_translate_cursor\n");

    #define P(x, y) vertices[i++] = x;  vertices[i++] = y; // Point vertex
    switch (rotation) {
    case HWC_ROTATE_NORMAL:
        y = h - y - ch - 1;

        P(x, y);
        P(x + cw, y);
        P(x, y + ch);
        P(x + cw, y + ch);
        break;
    case HWC_ROTATE_CW:
        t = x;
        x = h - y - 1;
        y = w - t - 1;

        P(x - ch, y - cw);
        P(x, y - cw);
        P(x - ch, y);
        P(x, y);
        break;
    case HWC_ROTATE_UD:
        x = w - x - 1;

        P(x - cw, y);
        P(x, y);
        P(x - cw, y + ch);
        P(x, y + ch);
        break;
    case HWC_ROTATE_CCW:
        t = x;
        x = y;
        y = t;

        P(x, y);
        P(x + ch, y);
        P(x, y + cw);
        P(x + ch, y + cw);
        break;
    }
    #undef P
}

void hwc_egl_render_cursor(ScreenPtr pScreen, int disp) {
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
	hwc_display_ptr display = NULL;
	if (disp == HWC_DISPLAY_PRIMARY) {
		display = &hwc->primary_display;
	} else {
		display = &hwc->external_display;
	}
	hwc_renderer_ptr renderer = &display->hwc_renderer;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_render_cursor disp: %d\n", disp);

    glUseProgram(renderer->projShader.program);

    glBindTexture(GL_TEXTURE_2D, hwc->cursorTexture);
    glUniform1i(renderer->projShader.texture, 0);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    hwc_translate_cursor(display->rotation, hwc->cursorX, hwc->cursorY,
                         hwc->cursorWidth, hwc->cursorHeight,
                         pScrn->virtualX, pScrn->virtualY,
                         cursorVertices);

    glVertexAttribPointer(renderer->projShader.position, 2, GL_FLOAT, 0, 0, cursorVertices);
    glEnableVertexAttribArray(renderer->projShader.position);

    glVertexAttribPointer(renderer->projShader.texcoords, 2, GL_FLOAT, 0, 0, textureVertices[display->rotation]);
    glEnableVertexAttribArray(renderer->projShader.texcoords);

    glUniformMatrix4fv(renderer->projShader.transform, 1, GL_FALSE, renderer->projection);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);
    glDisableVertexAttribArray(renderer->projShader.position);
    glDisableVertexAttribArray(renderer->projShader.texcoords);
}

void *hwc_egl_renderer_thread(void *user_data)
{
    ScreenPtr pScreen = (ScreenPtr)user_data;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    HWCPtr hwc = HWCPTR(pScrn);
    bool external_initialised = FALSE;

	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_thread\n");

	hwc_egl_renderer_screen_init(pScreen, 0);

    while (hwc->rendererIsRunning) {
        pthread_mutex_lock(&(hwc->dirtyLock));

        while (!hwc->dirty) {
            pthread_cond_wait(&(hwc->dirtyCond), &(hwc->dirtyLock));
        }

        hwc->dirty = FALSE;
        pthread_mutex_unlock(&(hwc->dirtyLock));

        if ((hwc->connected_outputs & 2) && !external_initialised) {
			hwc_egl_renderer_screen_init(pScreen, 1);
			external_initialised = TRUE;
		}

        pthread_mutex_lock(&(hwc->rendererLock));
        if (hwc->egl_fence != EGL_NO_SYNC_KHR) {
            eglClientWaitSyncKHR(hwc->egl_display,
								 hwc->egl_fence,
                EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                EGL_FOREVER_KHR);
        }
		if (hwc->primary_display.dpmsMode == DPMSModeOn)
			hwc_egl_renderer_update(pScreen, 0);
		if (hwc->external_display.dpmsMode == DPMSModeOn && external_initialised)
			hwc_egl_renderer_update(pScreen, 1);
        pthread_mutex_unlock(&(hwc->rendererLock));
    }

    if (external_initialised) {
		hwc_egl_renderer_screen_close(pScreen, 1);
	}
	hwc_egl_renderer_screen_close(pScreen, 0);

    return NULL;
}

void hwc_egl_renderer_update(ScreenPtr pScreen, int disp)
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

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update surface: %p, context: %p\n", renderer->surface, renderer->context);

	assert(eglMakeCurrent(hwc->egl_display, renderer->surface, renderer->surface, renderer->renderContext) == EGL_TRUE);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update viewport glamor: %d, width: %d, height: %d\n", hwc->glamor,display->width, display->height);

	if (hwc->glamor) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, display->width, display->height);
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

	glVertexAttribPointer(renderer->rootShader.texcoords, 2, GL_FLOAT, 0, 0, textureVertices[display->rotation]);
    glEnableVertexAttribArray(renderer->rootShader.texcoords);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 4\n");

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 5\n");

	glDisableVertexAttribArray(renderer->rootShader.position);
    glDisableVertexAttribArray(renderer->rootShader.texcoords);

//	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "hwc_egl_renderer_update 6\n");

	if (hwc->cursorShown)
        hwc_egl_render_cursor(pScreen, disp);

    eglSwapBuffers (hwc->egl_display, renderer->surface);  // get the rendered buffer to the screen
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
