package com.sbro.emucorev.data

import com.sbro.emucorev.core.VitaSfoData
import com.sbro.emucorev.core.VitaSfoParser
import java.io.File

/** APP_VER from the active update is the revision the guest actually runs. */
fun readInstalledGameMetadata(appDirectory: File): VitaSfoData {
    val base = runCatching { VitaSfoParser.parse(File(appDirectory, "sce_sys/param.sfo")) }
        .getOrDefault(VitaSfoData())
    val titleId = base.titleId ?: appDirectory.name
    if (titleId.isBlank() || titleId == "." || titleId == ".." || titleId.any { it == '/' || it == '\\' }) return base
    val ux0 = appDirectory.parentFile?.parentFile ?: return base
    val update = runCatching { VitaSfoParser.parse(File(ux0, "patch/$titleId/sce_sys/param.sfo")) }
        .getOrDefault(VitaSfoData())
    return if (update.titleId.equals(titleId, ignoreCase = true) && !update.version.isNullOrBlank()) {
        base.copy(version = update.version)
    } else base
}
