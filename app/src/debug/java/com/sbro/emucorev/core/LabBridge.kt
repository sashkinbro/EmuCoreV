package com.sbro.emucorev.core

/** Debug-only JNI entry of the lab hooks (tools/oracles/core-hooks/lab.cpp, ECV_ORACLE builds). */
object LabBridge {
    external fun nativeConfigure(runId: String, title: String, perfIntervalSeconds: Int, hasState: Boolean, captureFrames: Int, gpuTrace: Boolean, captureOpts: String)
}
