package com.sbro.emucorev.ui.cheats

import org.junit.Assert.assertEquals
import org.junit.Test

class CheatVersionCompatibilityTest {
    @Test
    fun comparesNumericVersionsWithoutLeadingZeroDifferences() {
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("01.01", "1.01"))
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("01.00", "1.0"))
        assertEquals(CheatVersionCompatibility.MISMATCH, cheatVersionCompatibility("01.02", "1.01"))
    }

    @Test
    fun supportsOnlyExplicitVersionsInLists() {
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("1.03", "1.00/1.03"))
        assertEquals(CheatVersionCompatibility.MISMATCH, cheatVersionCompatibility("1.02", "1.00/1.03"))
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("1.20", "1.00, 1.20"))
    }

    @Test
    fun supportsInclusiveRangesWithoutConfusingMinorDigits() {
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("1.06", "1.00-1.06"))
        assertEquals(CheatVersionCompatibility.MISMATCH, cheatVersionCompatibility("1.10", "1.00-1.06"))
        assertEquals(CheatVersionCompatibility.MISMATCH, cheatVersionCompatibility("1.01", "1.06-1.00"))
    }

    @Test
    fun neverClaimsUnknownOrModifiedVersionsAreCompatible() {
        for (version in listOf("", "latest", "Unknown", "NoNpDrm", "1.00? / 2.1.0", "1.03  EN-Trans")) {
            assertEquals(version, CheatVersionCompatibility.UNKNOWN, cheatVersionCompatibility("1.03", version))
        }
        assertEquals(CheatVersionCompatibility.UNKNOWN, cheatVersionCompatibility(null, "1.00"))
    }

    @Test
    fun usesExplicitNumberWhenLatestIsOnlyAnAnnotation() {
        assertEquals(CheatVersionCompatibility.MATCH, cheatVersionCompatibility("1.01", "01.01 (Latest)"))
        assertEquals(CheatVersionCompatibility.MISMATCH, cheatVersionCompatibility("1.02", "01.01 (Latest)"))
    }

    @Test
    fun catalogShowsOnlySelectedTitleAndHidesKnownOtherVersionsByDefault() {
        val packs = listOf(
            entry("exact", "PCSE00001", "1.02"),
            entry("other-version", "PCSE00001", "1.00"),
            entry("unknown", "PCSE00001", ""),
            entry("other-region", "PCSB00001", "1.02")
        )
        assertEquals(listOf("exact", "unknown"), visibleCheatPacks(packs, "pcse00001", "1.02", false).map { it.id })
        assertEquals(listOf("exact", "unknown", "other-version"), visibleCheatPacks(packs, "PCSE00001", "1.02", true).map { it.id })
        assertEquals(emptyList<String>(), visibleCheatPacks(packs, "", "1.02", true).map { it.id })
    }

    private fun entry(id: String, titleId: String, version: String) = CheatCatalogEntry(
        id, titleId, "Game", "USA", version, "Author", "", 1,
        "https://example.test/$id.psv", "https://example.test/source"
    )
}
