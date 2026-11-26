package com.sbro.emucorev.ui.emulation

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35])
class VitaLoadingDialogTest {
    @Test fun withoutAPackTheDialogIsUnavailable() {
        val empty = File.createTempFile("vita-pack", "").apply { delete(); mkdirs() }
        try {
            assertNull(VitaDialogAssets.load(empty))
        } finally {
            empty.deleteRecursively()
        }
    }

    @Test fun busyIndicatorEmitsTrailDotsAndKeepsHeadOnTheRing() {
        val busy = VitaBusyIndicator(72f, 16f).apply { start() }
        repeat(60) { busy.tick(1f / 60f) }
        assertTrue(busy.dots[0].size == 16f)
        assertTrue(busy.dots.drop(1).any { it.alpha > 0 && it.size > 0f })
        val radius = Math.hypot(busy.dots[0].x.toDouble(), busy.dots[0].y.toDouble())
        assertEquals(28.0, radius, 0.01)
    }

    /**
     * Host preview of the dialog from a pack extracted on the host: set VITA_PACK to the pack
     * directory and VITA_PREVIEW_OUT to an output prefix. Skipped otherwise.
     */
    @Test fun rendersPreviewImagesFromAPack() {
        val pack = System.getenv("VITA_PACK")
        val out = System.getenv("VITA_PREVIEW_OUT")
        assumeTrue(pack != null && out != null)
        val assets = requireNotNull(VitaDialogAssets.load(File(pack!!)))
        val message = System.getenv("VITA_PREVIEW_TEXT") ?: "Loading the saved state..."
        for ((name, progress) in listOf("0" to 0f, "45" to 0.45f, "100" to 1f)) {
            render(assets, message, progress, null, "$out-$name.png")
        }
        val busy = VitaBusyIndicator(72f, 16f).apply { start() }
        repeat(75) { busy.tick(1f / 60f) }
        render(assets, message, null, busy, "$out-busy.png")
    }

    private fun render(assets: VitaDialogAssets, message: String, progress: Float?, busy: VitaBusyIndicator?, path: String) {
        val bitmap = Bitmap.createBitmap(1920, 1088, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.BLACK)
        VitaLoadingDialogRenderer.draw(canvas, 1920f, 1088f, assets, message, progress, busy)
        File(path).outputStream().use { assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        assertNotEquals(Color.BLACK, bitmap.getPixel(960, 544))
    }
}
