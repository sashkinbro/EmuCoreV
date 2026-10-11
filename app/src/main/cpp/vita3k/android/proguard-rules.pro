# this is necessary so that native functions can call Java ones using JNI
-keep,allowoptimization,includedescriptorclasses class com.sbro.emucorev.core.sdl.** { *; }
