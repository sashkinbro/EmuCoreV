package com.sbro.emucorev.nativeui

import android.graphics.Typeface
import android.util.Log
import java.io.File

/**
 * A pack font (extracted from the installed firmware: ltn0 regular, ltn4 bold), or
 * the platform font when the pack lacks it. Never throws for a missing font.
 */
fun shellTypeface(resources: File, name: String, bold: Boolean): Typeface {
    for (extension in arrayOf("otf", "ttf")) {
        val file = File(resources, "fonts/$name.$extension")
        if (file.isFile) {
            try { return Typeface.createFromFile(file) } catch (error: RuntimeException) {
                Log.w("ShellTypefaces", "Unreadable pack font ${file.name}", error)
            }
        }
    }
    return if (bold) Typeface.DEFAULT_BOLD else Typeface.DEFAULT
}
