package com.sbro.emucorev.core

enum class FirmwareKind {
    Base,
    Update
}

data class FirmwareSource(
    val kind: FirmwareKind,
    val version: String,
    val url: String,
    val approximateSizeBytes: Long,
    val fileName: String
)

object FirmwareSources {
    val base: FirmwareSource = FirmwareSource(
        kind = FirmwareKind.Base,
        version = "3.74",
        url = "http://dus01.psv.update.playstation.net/update/psv/image/2022_0209/rel_f2c7b12fe85496ec88a0391b514d6e3b/PSVUPDAT.PUP",
        approximateSizeBytes = 133_676_544L,
        fileName = "PSVUPDAT.PUP"
    )

    val update: FirmwareSource = FirmwareSource(
        kind = FirmwareKind.Update,
        version = "3.74 fonts",
        url = "http://dus01.psp2.update.playstation.net/update/psp2/image/2022_0209/sd_59dcf059d3328fb67be7e51f8aa33418/PSP2UPDAT.PUP?dest=us",
        approximateSizeBytes = 21_718_912L,
        fileName = "PSP2UPDAT.PUP"
    )

    fun forKind(kind: FirmwareKind): FirmwareSource = when (kind) {
        FirmwareKind.Base -> base
        FirmwareKind.Update -> update
    }
}
