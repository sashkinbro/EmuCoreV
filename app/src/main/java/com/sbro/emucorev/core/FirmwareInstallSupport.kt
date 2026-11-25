package com.sbro.emucorev.core

/** Outcome of the native firmware installer; mirrors `emucorev::firmware::Status`. */
enum class FirmwareInstallStatus {
    OK,
    NOT_PUP,
    TRUNCATED,
    CORRUPT,
    IO_ERROR,
    NO_SPACE,
    CANCELLED,
    INTERNAL
}

data class FirmwareInstallResult(
    val status: FirmwareInstallStatus,
    /** version.txt of the package (OK only; empty when the package has none). */
    val version: String = "",
    /** Partition -> files installed (OK only). */
    val partitionFiles: Map<String, Long> = emptyMap(),
    /** Bytes the installer wanted free (NO_SPACE only). */
    val requiredBytes: Long = 0L,
    /** Native diagnostic, English, for logs. */
    val detail: String? = null
) {
    val success: Boolean get() = status == FirmwareInstallStatus.OK

    companion object {
        /** Parses the string returned by `nativeInstallFirmware`: `<STATUS>|<payload>`. */
        fun parse(raw: String?): FirmwareInstallResult {
            if (raw.isNullOrEmpty()) return FirmwareInstallResult(FirmwareInstallStatus.INTERNAL, detail = "no answer")
            val parts = raw.split('|', limit = 3)
            val status = FirmwareInstallStatus.entries.firstOrNull { it.name == parts[0] }
                ?: return FirmwareInstallResult(FirmwareInstallStatus.INTERNAL, detail = raw)
            return when (status) {
                FirmwareInstallStatus.OK -> FirmwareInstallResult(
                    status,
                    version = parts.getOrElse(1) { "" },
                    partitionFiles = parsePartitions(parts.getOrElse(2) { "" })
                )
                FirmwareInstallStatus.NO_SPACE -> FirmwareInstallResult(
                    status,
                    requiredBytes = parts.getOrElse(1) { "" }.toLongOrNull() ?: 0L
                )
                else -> FirmwareInstallResult(status, detail = raw.substringAfter('|', ""))
            }
        }

        private fun parsePartitions(text: String): Map<String, Long> =
            text.split(',').mapNotNull { entry ->
                val name = entry.substringBefore(':', "")
                val files = entry.substringAfter(':', "").toLongOrNull()
                if (name.isNotEmpty() && files != null) name to files else null
            }.toMap()
    }
}

/**
 * Early free-space check before the native installer starts.
 *
 * The native installer does its own, exact preflight (it knows the decrypted size of every partition) and answers NO_SPACE
 * with the byte count it needs; this one only rejects hopeless cases without touching the disk.
 *
 * Measured with the real 3.74 packages (host harness `app/src/test/fwinstall-host`, peak size of the Vita root during an
 * install into an empty root): base package 133.8 MB -> 231 MB (1.7x), fonts package 56.8 MB (deflated sa0, 94.7 MB on
 * disk) -> 192 MB (3.4x). Replacing an existing partition keeps the old tree until the swap, which adds the size of the
 * tree being replaced (about 1.0x / 1.7x). [FACTOR] x package is therefore the lower bound for the base package and
 * deliberately not for the fonts one; the exact native estimate covers that.
 */
object FirmwareInstallPreflight {
    const val FACTOR = 3L
    const val MARGIN_BYTES = 64L * 1024 * 1024

    fun requiredBytes(packageBytes: Long): Long = packageBytes.coerceAtLeast(0L) * FACTOR + MARGIN_BYTES

    fun hasEnoughSpace(availableBytes: Long, packageBytes: Long): Boolean =
        availableBytes >= requiredBytes(packageBytes)
}

/** Native progress detail of the firmware installer: `<phase>:<partition>:<files>`. */
data class FirmwareProgressDetail(
    val phase: Phase,
    val partition: String,
    val files: Long
) {
    enum class Phase { VALIDATE, DECRYPT, EXTRACT, COMMIT }

    companion object {
        fun parse(detail: String?): FirmwareProgressDetail? {
            if (detail.isNullOrEmpty()) return null
            val parts = detail.split(':', limit = 3)
            if (parts.size < 2) return null
            val phase = when (parts[0]) {
                "validate" -> Phase.VALIDATE
                "decrypt" -> Phase.DECRYPT
                "extract" -> Phase.EXTRACT
                "commit" -> Phase.COMMIT
                else -> return null
            }
            return FirmwareProgressDetail(phase, parts[1], parts.getOrNull(2)?.toLongOrNull() ?: 0L)
        }
    }
}
