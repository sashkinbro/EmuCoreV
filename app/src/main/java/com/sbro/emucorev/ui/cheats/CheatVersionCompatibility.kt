package com.sbro.emucorev.ui.cheats

enum class CheatVersionCompatibility { MATCH, UNKNOWN, MISMATCH }

private val versionPattern = Regex("(?i)^v?(\\d{1,3}(?:\\.\\d{1,3}){1,2})(?:\\s*\\(latest\\))?$")

private fun numericVersion(value: String?): List<Int>? = value?.trim()?.let { text ->
    versionPattern.matchEntire(text)?.groupValues?.get(1)?.split('.')?.map(String::toInt)
}

private fun compareVersions(first: List<Int>, second: List<Int>): Int {
    for (index in 0 until maxOf(first.size, second.size)) {
        val result = (first.getOrElse(index) { 0 }).compareTo(second.getOrElse(index) { 0 })
        if (result != 0) return result
    }
    return 0
}

/** Only explicit numeric revisions, lists and inclusive ranges imply compatibility. */
fun cheatVersionCompatibility(installedVersion: String?, packVersion: String): CheatVersionCompatibility {
    val installed = numericVersion(installedVersion) ?: return CheatVersionCompatibility.UNKNOWN
    val alternatives = packVersion.split(Regex("\\s*[/,]\\s*"))
    var matches = false
    for (alternative in alternatives) {
        val range = alternative.split(Regex("\\s*[-–]\\s*"))
        if (range.size !in 1..2) return CheatVersionCompatibility.UNKNOWN
        val first = numericVersion(range[0]) ?: return CheatVersionCompatibility.UNKNOWN
        val last = if (range.size == 2) numericVersion(range[1]) ?: return CheatVersionCompatibility.UNKNOWN else first
        matches = matches || (compareVersions(installed, first) >= 0 && compareVersions(installed, last) <= 0)
    }
    return if (matches) CheatVersionCompatibility.MATCH else CheatVersionCompatibility.MISMATCH
}

fun visibleCheatPacks(
    entries: List<CheatCatalogEntry>,
    titleId: String,
    installedVersion: String?,
    showOtherVersions: Boolean
): List<CheatCatalogEntry> = entries
    .filter { titleId.isNotBlank() && it.titleId.equals(titleId, ignoreCase = true) }
    .filter { showOtherVersions || cheatVersionCompatibility(installedVersion, it.version) != CheatVersionCompatibility.MISMATCH }
    .sortedBy { cheatVersionCompatibility(installedVersion, it.version).ordinal }
