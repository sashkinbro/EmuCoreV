# ECV uses the upstream SDL JNI package for native callbacks and class lookup.
-keep,allowoptimization,includedescriptorclasses class org.libsdl.app.** { *; }
