package com.sbro.emucorev.core.vita

import android.app.Activity
import android.os.Bundle

/**
 * Builds without the local native UI resource pack keep the Compose carousel;
 * NativeLibraryMode.available is false there, so this is never started.
 */
class NativeShellHostActivity : Activity(), NativeLibraryMode.ReturnTarget {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        finish()
    }
}
