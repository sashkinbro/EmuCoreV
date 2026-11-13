package com.sbro.emucorev.core

import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLSurface
import android.opengl.GLES20
import androidx.annotation.WorkerThread
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** A fresh thread owns every context/surface operation; no engine context is made current here. */
internal object GpuRendererQuery {
    @WorkerThread
    fun query(): String? = runBoundedGpuProbe(1500) { queryGpuRendererSession(AndroidGpuRendererProbe()) }
}

/** A broken vendor call may outlive the deadline; there is only one probe attempt per process. */
internal fun runBoundedGpuProbe(timeoutMillis: Long, query: () -> String?): String? {
    val executor = Executors.newSingleThreadExecutor { task ->
        Thread(task, "emucorev-gpu-probe").apply { isDaemon = true }
    }
    val result = executor.submit<String?> { query() }
    return try {
        result.get(timeoutMillis, TimeUnit.MILLISECONDS)
    } catch (_: InterruptedException) {
        Thread.currentThread().interrupt()
        null
    } catch (_: Exception) {
        null
    } finally {
        result.cancel(true)
        executor.shutdownNow()
    }
}

internal interface GpuRendererProbeBackend {
    fun initialize(): Boolean
    fun createContext(): Boolean
    fun createSurface(): Boolean
    fun makeCurrent(): Boolean
    fun renderer(): String?
    fun clearCurrent()
    fun destroySurface()
    fun destroyContext()
    fun terminate()
    fun releaseThread()
}

internal fun queryGpuRendererSession(backend: GpuRendererProbeBackend): String? = try {
    if (!backend.initialize() || !backend.createContext() || !backend.createSurface() || !backend.makeCurrent()) null
    else backend.renderer()?.takeIf(String::isNotBlank)
} catch (_: Exception) {
    null
} catch (_: LinkageError) {
    null
} finally {
    // One failed cleanup call must not skip the remaining owned resources.
    runCatching { backend.clearCurrent() }
    runCatching { backend.destroySurface() }
    runCatching { backend.destroyContext() }
    runCatching { backend.terminate() }
    runCatching { backend.releaseThread() }
}

private class AndroidGpuRendererProbe : GpuRendererProbeBackend {
    private var display: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var context: EGLContext = EGL14.EGL_NO_CONTEXT
    private var surface: EGLSurface = EGL14.EGL_NO_SURFACE
    private var initialized = false
    private var current = false
    private var config: EGLConfig? = null

    override fun initialize(): Boolean {
        display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (display == EGL14.EGL_NO_DISPLAY) return false
        val version = IntArray(2)
        initialized = EGL14.eglInitialize(display, version, 0, version, 1)
        return initialized
    }

    override fun createContext(): Boolean {
        val configs = arrayOfNulls<EGLConfig>(1)
        val count = IntArray(1)
        val attributes = intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_NONE
        )
        if (!EGL14.eglChooseConfig(display, attributes, 0, configs, 0, 1, count, 0) || count[0] < 1) return false
        config = configs[0] ?: return false
        context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT,
            intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE), 0)
        return context != EGL14.EGL_NO_CONTEXT
    }

    override fun createSurface(): Boolean {
        surface = EGL14.eglCreatePbufferSurface(display, config,
            intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
        return surface != EGL14.EGL_NO_SURFACE
    }

    override fun makeCurrent(): Boolean {
        current = EGL14.eglMakeCurrent(display, surface, surface, context)
        return current
    }

    override fun renderer(): String? = GLES20.glGetString(GLES20.GL_RENDERER)
    override fun clearCurrent() {
        if (current) EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
    }
    override fun destroySurface() {
        if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface)
    }
    override fun destroyContext() {
        if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context)
    }
    override fun terminate() {
        // Balance only this successful Android EGL initialization (display refs are counted).
        if (initialized) EGL14.eglTerminate(display)
    }
    override fun releaseThread() { EGL14.eglReleaseThread() }
}
