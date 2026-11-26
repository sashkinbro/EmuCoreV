package com.sbro.emucorev.core

/** Debug-only JNI entry of the lab hooks (tools/oracles/core-hooks/lab.cpp, ECV_ORACLE builds). */
object LabBridge {
    external fun nativeConfigure(runId: String, title: String, perfIntervalSeconds: Int, hasState: Boolean, captureFrames: Int, gpuTrace: Boolean, captureOpts: String)
    /** tools/oracles/core-hooks/lab_input.cpp: schedules one lab input command line, returns "ok id=..." or "error: ...". */
    external fun nativeInput(cmd: String): String
}
