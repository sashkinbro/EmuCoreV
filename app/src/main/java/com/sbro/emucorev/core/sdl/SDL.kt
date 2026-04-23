package com.sbro.emucorev.core.sdl

import android.content.Context
import java.lang.reflect.Method

class SDL {
    companion object {
        private var mContext: Context? = null

        @JvmStatic
        fun setupJNI() {
            SDLActivity.nativeSetupJNI()
            SDLAudioManager.nativeSetupJNI()
            SDLControllerManager.nativeSetupJNI()
        }

        @JvmStatic
        fun initialize() {
            setContext(null)
            SDLActivity.initialize()
            SDLAudioManager.initialize()
            SDLControllerManager.initialize()
        }

        @JvmStatic
        fun setContext(context: Context?) {
            SDLAudioManager.setContext(context)
            mContext = context
        }

        @JvmStatic
        fun getContext(): Context {
            return requireNotNull(mContext) { "SDL context has not been initialized." }
        }

        @JvmStatic
        @Throws(UnsatisfiedLinkError::class, SecurityException::class, NullPointerException::class)
        fun loadLibrary(libraryName: String?) {
            loadLibrary(libraryName, mContext)
        }

        @JvmStatic
        @Throws(UnsatisfiedLinkError::class, SecurityException::class, NullPointerException::class)
        fun loadLibrary(libraryName: String?, context: Context?) {
            require(!libraryName.isNullOrBlank()) { "No library name provided." }
            val resolvedContext = requireNotNull(context) { "No context available to load library $libraryName." }

            try {
                val classLoader = resolvedContext.classLoader
                val relinkClass = classLoader.loadClass("com.getkeepsafe.relinker.ReLinker")
                val relinkListenerClass = classLoader.loadClass("com.getkeepsafe.relinker.ReLinker\$LoadListener")
                val contextClass = classLoader.loadClass("android.content.Context")
                val stringClass = classLoader.loadClass("java.lang.String")

                val forceMethod: Method = relinkClass.getDeclaredMethod("force")
                val relinkInstance = forceMethod.invoke(null)
                val relinkInstanceClass = relinkInstance.javaClass
                val loadMethod = relinkInstanceClass.getDeclaredMethod(
                    "loadLibrary",
                    contextClass,
                    stringClass,
                    stringClass,
                    relinkListenerClass
                )
                loadMethod.invoke(relinkInstance, resolvedContext, libraryName, null, null)
            } catch (_: Throwable) {
                System.loadLibrary(libraryName)
            }
        }
    }
}
