package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class RetroArchShaderPresetParserTest {
    @Test
    fun acceptsMultipassPresetWithTexturesIncludesAndParameters() {
        val root = kotlin.io.path.createTempDirectory("shader-valid-").toFile()
        try {
            File(root, "shaders/passes").mkdirs()
            File(root, "shaders/textures").mkdirs()
            File(root, "shaders/common.glsl").writeText("// shared source\n")
            File(root, "shaders/passes/first.slang").writeText(
                "#include \"../common.glsl\"\n#pragma parameter BRIGHTNESS \"Brightness\" 1.0 0.0 2.0 0.1\n"
            )
            File(root, "shaders/passes/second.slang").writeText("void main() {}\n")
            File(root, "shaders/textures/lookup.png").writeBytes(byteArrayOf(1, 2, 3))
            File(root, "shaders/chain.slangp").writeText(
                "shaders = 2\nshader0 = \"passes/first.slang\"\nshader1 = \"passes/second.slang\"\ntextures = \"LUT\"\nLUT = \"textures/lookup.png\"\n"
            )

            val result = RetroArchShaderPresetParser.inspect(File(root, "shaders/chain.slangp"), root)

            assertTrue(result.error, result.isValid)
            assertEquals(2, result.passCount)
            assertEquals(listOf("BRIGHTNESS"), result.parameterIds)
            assertEquals(1.0f, result.parameters.single().initialValue)
            assertEquals(0.1f, result.parameters.single().step)
            assertEquals(listOf("LUT"), result.textureIds)
        } finally {
            root.deleteRecursively()
        }
    }

    @Test
    fun rejectsPresetReferencesOutsideInstalledPack() {
        val root = kotlin.io.path.createTempDirectory("shader-traversal-").toFile()
        try {
            File(root, "chain.slangp").writeText("shaders = 1\nshader0 = \"../outside.slang\"\n")
            File(root.parentFile, "outside.slang").writeText("void main() {}\n")

            val result = RetroArchShaderPresetParser.inspect(File(root, "chain.slangp"), root)

            assertFalse(result.isValid)
            assertTrue(result.error.orEmpty().contains("outside"))
        } finally {
            root.deleteRecursively()
            File(root.parentFile, "outside.slang").delete()
        }
    }

    @Test
    fun rejectsIncompletePassAndExcessivePassCount() {
        val root = kotlin.io.path.createTempDirectory("shader-invalid-").toFile()
        try {
            val preset = File(root, "chain.slangp")
            preset.writeText("shaders = 2\nshader0 = \"pass.slang\"\n")
            File(root, "pass.slang").writeText("void main() {}\n")

            val missingPass = RetroArchShaderPresetParser.inspect(preset, root)
            assertFalse(missingPass.isValid)

            preset.writeText("shaders = 65\n")
            val tooMany = RetroArchShaderPresetParser.inspect(preset, root)
            assertFalse(tooMany.isValid)
        } finally {
            root.deleteRecursively()
        }
    }
}
