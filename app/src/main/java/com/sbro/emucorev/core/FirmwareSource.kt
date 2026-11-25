package com.sbro.emucorev.core

enum class FirmwareKind {
    Base,
    Update
}

/**
 * A firmware package the app can download.
 *
 * [sizeBytes] and [sha256] pin the exact file the update servers serve (Last-Modified 2022-02-08; the md5 in the base URL
 * and both ETags match the files the hashes were computed from). [FirmwareDownloader] rejects anything else, so a
 * truncated, tampered or substituted download never reaches the installer.
 */
data class FirmwareSource(
    val kind: FirmwareKind,
    val version: String,
    /** Canonical URL as published by the update servers (cleartext http). */
    val url: String,
    val sizeBytes: Long,
    /** Lower-case hex SHA-256 of the complete file. */
    val sha256: String,
    val fileName: String
) {
    /** https is tried first, plain http only when the secure connection cannot be established. */
    val candidateUrls: List<String>
        get() = if (url.startsWith("http://")) listOf("https://" + url.removePrefix("http://"), url) else listOf(url)
}

object FirmwareSources {
    val base: FirmwareSource = FirmwareSource(
        kind = FirmwareKind.Base,
        version = "3.74",
        url = "http://dus01.psv.update.playstation.net/update/psv/image/2022_0209/rel_f2c7b12fe85496ec88a0391b514d6e3b/PSVUPDAT.PUP",
        sizeBytes = 133_834_240L,
        sha256 = "6ef6dc8da6db026f28647713e473486d770087a605c52a8d751bfca7478386cf",
        fileName = "PSVUPDAT.PUP"
    )

    val update: FirmwareSource = FirmwareSource(
        kind = FirmwareKind.Update,
        version = "3.74 fonts",
        url = "http://dus01.psp2.update.playstation.net/update/psp2/image/2022_0209/sd_59dcf059d3328fb67be7e51f8aa33418/PSP2UPDAT.PUP?dest=us",
        sizeBytes = 56_778_752L,
        sha256 = "c3c03fc7363dd573d90e5157629bf11551f434b283cc898d9ffc71dd716b791c",
        fileName = "PSP2UPDAT.PUP"
    )

    fun forKind(kind: FirmwareKind): FirmwareSource = when (kind) {
        FirmwareKind.Base -> base
        FirmwareKind.Update -> update
    }
}
