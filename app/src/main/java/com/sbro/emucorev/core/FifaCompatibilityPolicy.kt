package com.sbro.emucorev.core

import java.util.Locale

/** Runtime compatibility override; never changes the global defaults. */
object FifaCompatibilityPolicy {
    // Verified against Vita3K/compatibility FIFA reports. Also covers renamed mods.
    internal val titleIds = setOf(
        "PCSB00051", "PCSB00052", "PCSG00039",
        "PCSB00082", "PCSB00083", "PCSB00084", "PCSB00085", "PCSB00086",
        "PCSE00055", "PCSE00059", "PCSE00060", "PCSG90012",
        "PCSB00170", "PCSB00171", "PCSB00174", "PCSE00093", "PCSE00096", "PCSG00107",
        "PCSB00339", "PCSB00340", "PCSE00263", "PCSG00201",
        "PCSB00603", "PCSB00604", "PCSB00605", "PCSB00606", "PCSB00607",
        "PCSE00481", "PCSE00482", "PCSE00483", "PCSG00404"
    )
    private val fifaName = Regex("(?<![A-Z])FIFA(?![A-Z])")

    fun appliesTo(titleId: String, title: String = ""): Boolean =
        titleId.trim().uppercase(Locale.ROOT) in titleIds ||
            fifaName.containsMatchIn(title.uppercase(Locale.ROOT))

    fun apply(config: VitaCoreConfig, titleId: String, title: String = ""): VitaCoreConfig =
        if (appliesTo(titleId, title)) {
            // Native Buffer is a Vulkan mapping method, not an OpenGL option.
            config.copy(memoryMapping = "native-buffer", backendRenderer = "Vulkan", useAngle = false)
        } else {
            config
        }
}
