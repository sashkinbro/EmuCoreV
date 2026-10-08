package com.sbro.emucorev.ui.cheats

import com.sbro.emucorev.core.VitaCheatSnapshot
import java.io.ByteArrayOutputStream
import java.security.MessageDigest

private fun digest(bytes: ByteArray): String = MessageDigest.getInstance("SHA-256")
    .digest(bytes).joinToString("") { "%02x".format(it.toInt() and 0xff) }

/** Catalog digests are defined over LF bytes, independent of checkout line endings. */
fun verifyCheatPack(bytes: ByteArray, expectedSha256: String): Boolean {
    if (expectedSha256.isBlank()) return true // Older catalog schemas did not require a digest.
    if (!expectedSha256.matches(Regex("[a-fA-F0-9]{64}"))) return false
    val canonical = ByteArrayOutputStream(bytes.size)
    var index = 0
    while (index < bytes.size) {
        if (bytes[index] == '\r'.code.toByte()) {
            canonical.write('\n'.code)
            if (index + 1 < bytes.size && bytes[index + 1] == '\n'.code.toByte()) ++index
        } else canonical.write(bytes[index].toInt())
        ++index
    }
    return digest(canonical.toByteArray()).equals(expectedSha256, ignoreCase = true)
}

/** Enabled flags may change after installation; names, order and codes identify the pack. */
fun cheatPackFingerprint(snapshot: VitaCheatSnapshot): String = digest(
    snapshot.cheats.joinToString("\u0000") { "${it.name}\u0001${it.codes}" }.toByteArray(Charsets.UTF_8)
)
