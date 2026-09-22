package com.sbro.emucorev.core

import org.json.JSONObject

data class VitaCheatEntry(
    val name: String,
    val enabled: Boolean,
    val enabledOnBoot: Boolean,
    val broken: Boolean,
    val codes: String
)

data class VitaCheatSnapshot(
    val titleId: String,
    val header: String,
    val path: String,
    val masterEnabled: Boolean,
    val cheats: List<VitaCheatEntry>
) {
    companion object {
        val EMPTY = VitaCheatSnapshot(
            titleId = "",
            header = "",
            path = "",
            masterEnabled = false,
            cheats = emptyList()
        )
    }
}

object CheatBridge {
    external fun getCheats(titleId: String): String?
    external fun setCheatEnabled(titleId: String, index: Int, enabled: Boolean): Boolean
    external fun setAllCheatsEnabled(titleId: String, enabled: Boolean): Boolean
    external fun setCheatsEnabled(enabled: Boolean): Boolean
    external fun saveCheats(titleId: String): Boolean
    external fun reloadCheats(titleId: String): String?
    external fun getCheatFilePath(titleId: String): String?
    external fun importCheatFile(titleId: String, sourcePath: String, displayName: String): String?
    external fun deleteCheats(titleId: String): Boolean

    fun snapshot(titleId: String): VitaCheatSnapshot = parse(getCheats(titleId))

    fun reload(titleId: String): VitaCheatSnapshot = parse(reloadCheats(titleId))

    fun importFile(titleId: String, sourcePath: String, displayName: String): VitaCheatSnapshot =
        parse(importCheatFile(titleId, sourcePath, displayName))

    private fun parse(raw: String?): VitaCheatSnapshot {
        if (raw.isNullOrBlank()) return VitaCheatSnapshot.EMPTY
        return runCatching {
            val root = JSONObject(raw)
            val cheats = root.optJSONArray("cheats")
            val entries = buildList {
                if (cheats != null) {
                    for (index in 0 until cheats.length()) {
                        val item = cheats.optJSONObject(index) ?: continue
                        add(
                            VitaCheatEntry(
                                name = item.optString("name"),
                                enabled = item.optBoolean("enabled"),
                                enabledOnBoot = item.optBoolean("enabledOnBoot"),
                                broken = item.optBoolean("broken"),
                                codes = item.optString("codes")
                            )
                        )
                    }
                }
            }
            VitaCheatSnapshot(
                titleId = root.optString("titleId"),
                header = root.optString("header"),
                path = root.optString("path"),
                masterEnabled = root.optBoolean("masterEnabled"),
                cheats = entries
            )
        }.getOrDefault(VitaCheatSnapshot.EMPTY)
    }
}
