package com.sbro.emucorev.ui.settings

import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readBytes
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class BrandingUiContractTest {
    @Test
    fun customizationPreviewKeepsItsOuterSizeWhileCoverTilesChange() {
        val source = appModuleRoot()
            .resolve("src/main/java/com/sbro/emucorev/ui/settings/CustomizationTab.kt")
            .readText()

        assertTrue("Preview must keep a stable outer height", ".height(210.dp)" in source)
        assertTrue("Only cover tiles should scale", ".width(52.dp * coverScale)" in source)
        assertTrue("Tile row must consume the fixed preview remainder", ".weight(1f)" in source)
        assertFalse(
            "Cover tiles must not use weight because that changes preview geometry",
            Regex("""\.weight\(1f\)\s*\.aspectRatio\(0\.72f\)""").containsMatchIn(source)
        )
    }

    @Test
    fun generatedBrandingPngsAreDistinct512PixelImages() {
        val drawableRoot = appModuleRoot().resolve("src/main/res/drawable-nodpi")
        val defaultIcon = drawableRoot.resolve("ic_drawer_app.png")
        val proIcon = drawableRoot.resolve("ic_drawer_app_pro.png")

        assertEquals(512 to 512, pngDimensions(defaultIcon))
        assertEquals(512 to 512, pngDimensions(proIcon))
        assertNotEquals(
            "The Pro icon must be a genuinely different gold asset",
            defaultIcon.readBytes().contentHashCode(),
            proIcon.readBytes().contentHashCode()
        )
    }

    @Test
    fun welcomeDialogAndDrawerSelectBrandingFromProState() {
        val sourceRoot = appModuleRoot().resolve("src/main/java/com/sbro/emucorev")
        val welcome = sourceRoot.resolve("ui/pro/ProPurchaseUi.kt").readText()
        val drawer = sourceRoot.resolve("navigation/AdaptiveShell.kt").readText()

        listOf(welcome, drawer).forEach { source ->
            assertTrue("Regular app icon must be available", "R.drawable.ic_drawer_app" in source)
            assertTrue("Gold app icon must be selected for Pro", "R.drawable.ic_drawer_app_pro" in source)
            assertTrue("Icon selection must follow Pro state", "isProUnlocked" in source)
        }
    }

    @Test
    fun iconGeneratorSupportsReproducibleCheckMode() {
        val script = appModuleRoot().parent.resolve("tools/generate_drawer_icons.py").readText()

        assertTrue("--check" in script)
        assertTrue("ic_launcher_pro_foreground.xml" in script || "ic_launcher{suffix}_foreground.xml" in script)
        assertTrue("ic_drawer_app_pro.png" in script)
    }

    private fun pngDimensions(path: Path): Pair<Int, Int> {
        val header = path.readBytes().take(24).toByteArray()
        assertTrue("$path must be a PNG", header.copyOfRange(0, 8).contentEquals(PNG_SIGNATURE))
        val dimensions = ByteBuffer.wrap(header, 16, 8).order(ByteOrder.BIG_ENDIAN)
        return dimensions.int to dimensions.int
    }

    private fun appModuleRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        return sequenceOf(
            workingDirectory,
            workingDirectory.resolve("app")
        ).firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
    }

    private companion object {
        val PNG_SIGNATURE = byteArrayOf(
            0x89.toByte(),
            0x50,
            0x4E,
            0x47,
            0x0D,
            0x0A,
            0x1A,
            0x0A
        )
    }
}
