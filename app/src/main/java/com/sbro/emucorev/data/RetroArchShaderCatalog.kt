package com.sbro.emucorev.data

import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.FileOutputStream

/** Rebuildable catalog; shared parameter tables keep large official packs compact. */
internal object RetroArchShaderCatalog {
    private const val NAME = ".preset-catalog-v3"
    private const val MAGIC = 0x45435633

    fun read(root: File): List<RetroArchShaderPreset>? = runCatching {
        val base = root.canonicalFile
        val prefix = base.path + File.separator
        val index = File(base, NAME)
        require(index.isFile && index.length() <= 64L * 1024 * 1024)
        DataInputStream(index.inputStream().buffered()).use { input ->
            require(input.readInt() == MAGIC)
            val groups = List(input.count(4096)) {
                List(input.count(RetroArchShaderPreset.MAX_PARAMETERS)) {
                    RetroArchShaderParameter(input.readUTF(), input.readUTF(), input.readFloat(),
                        input.readFloat(), input.readFloat(), input.readFloat())
                }
            }
            List(input.count(4096)) {
                val relative = input.readUTF()
                val file = File(base, relative).canonicalFile
                require(!File(relative).isAbsolute && file.path.startsWith(prefix))
                val passes = input.readInt().also { require(it in 1..64) }
                val textures = List(input.count(4096)) { input.readUTF() }
                val parameters = groups[input.readInt()]
                RetroArchShaderPreset(relative.substringBeforeLast('.'), file.path, passes, textures, parameters)
            }.also { require(input.read() == -1) }
        }
    }.getOrNull()

    fun write(root: File, presets: List<RetroArchShaderPreset>) {
        val temporary = File(root, "$NAME.tmp")
        try {
            FileOutputStream(temporary).use { file ->
                val output = DataOutputStream(file.buffered())
                output.writeInt(MAGIC)
                val groups = presets.map { it.parameters }.distinct()
                val groupIds = groups.withIndex().associate { it.value to it.index }
                output.writeInt(groups.size)
                groups.forEach { parameters ->
                    output.writeInt(parameters.size)
                    parameters.forEach { p ->
                        output.writeUTF(p.id); output.writeUTF(p.description)
                        output.writeFloat(p.initialValue); output.writeFloat(p.minimum)
                        output.writeFloat(p.maximum); output.writeFloat(p.step)
                    }
                }
                output.writeInt(presets.size)
                presets.forEach { preset ->
                    output.writeUTF(File(preset.absolutePath).relativeTo(root).invariantSeparatorsPath)
                    output.writeInt(preset.passCount)
                    output.writeInt(preset.textureIds.size)
                    preset.textureIds.forEach(output::writeUTF)
                    output.writeInt(groupIds.getValue(preset.parameters))
                }
                output.flush()
                file.fd.sync()
            }
            java.nio.file.Files.move(temporary.toPath(), File(root, NAME).toPath(),
                java.nio.file.StandardCopyOption.REPLACE_EXISTING)
        } finally { temporary.delete() }
    }

    private fun DataInputStream.count(maximum: Int): Int = readInt().also { require(it in 0..maximum) }
}
