package com.sbro.emucorev.ui.emulation

import android.graphics.Bitmap
import java.io.File
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
@GraphicsMode(GraphicsMode.Mode.NATIVE)
class SaveStatePreviewTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun absentAndInvalidPreviewsHaveNoBitmap() {
        assertNull(decodeSaveStatePreview(null))
        assertNull(decodeSaveStatePreview(File(temporary.root, "absent.jpg").path))
        val invalid = temporary.newFile("invalid.jpg").apply { writeText("broken image") }
        assertNull(decodeSaveStatePreview(invalid.path))
    }

    @Test fun largePreviewIsSampledToBoundMemory() {
        val file = temporary.newFile("preview.png")
        val source = Bitmap.createBitmap(2880, 1632, Bitmap.Config.ARGB_8888)
        file.outputStream().use { source.compress(Bitmap.CompressFormat.PNG, 100, it) }
        source.recycle()
        val result = requireNotNull(decodeSaveStatePreview(file.path))
        assertTrue(result.width <= 720)
        assertTrue(result.height <= 408)
        assertTrue(result.allocationByteCount <= 720 * 408 * 4)
    }
}
