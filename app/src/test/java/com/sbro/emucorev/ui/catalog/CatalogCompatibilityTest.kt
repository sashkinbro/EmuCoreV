package com.sbro.emucorev.ui.catalog

import com.sbro.emucorev.data.*
import java.time.LocalDate
import org.junit.Assert.*
import org.junit.Test

class CatalogCompatibilityTest {
    private val date = LocalDate.of(2026, 11, 13)
    private fun run(id: String, model: String, testedOn: LocalDate = date): EcvCompatibilityRun {
        val unknown = EcvCompatibilityResult(EcvCompatibilityStatus.NOT_TESTED, emptyList())
        return EcvCompatibilityRun(id, EcvCompatibilityDevice("Synthetic maker", model, null, null),
            "fixture-app", "fixture-core", testedOn, emptyMap(),
            EcvCompatibilityResults(unknown, unknown, unknown, unknown, unknown, unknown), null)
    }
    private fun entry(id: Long, serials: List<String>, name: String = "Synthetic shared title") =
        VitaCatalogEntry(id, name, null, null, null, null, null, serials = serials)

    @Test fun catalogKeepsEachRegionalIdDeviceAndDateAsItsOwnRun() {
        val regionOne = listOf(run("a-old", "A", date.minusDays(1)), run("a-new", "A"), run("b", "B"))
        val regionTwo = listOf(run("other-region", "A"))
        val dataset = EcvCompatibilityDataset(1, 1, "EmuCoreV", date, listOf(
            EcvCompatibilityRecord("PCZZ99999", regionOne), EcvCompatibilityRecord("PCZZ99998", regionTwo)))
        val result = listOf(entry(1, listOf("PCZZ99999", "PCZZ99998", "PCZZ99999")))
            .attachEcvCompatibility(dataset).single()

        assertEquals(listOf("PCZZ99999", "PCZZ99999", "PCZZ99999", "PCZZ99998"), result.compatibility.map { it.titleId })
        assertEquals(listOf("a-old", "a-new", "b", "other-region"), result.compatibility.map { it.run.runId })
        assertEquals(date.minusDays(1), result.compatibility.first().run.testedOn)
        assertSame(regionOne[0], result.compatibility.first().run)
    }

    @Test fun sameNameAndNearbyOrMalformedIdNeverBecomeCompatibilityEvidence() {
        val dataset = EcvCompatibilityDataset(1, 1, "EmuCoreV", date, listOf(
            EcvCompatibilityRecord("PCZZ99999", listOf(run("known", "A")))))
        val results = listOf(entry(1, listOf("PCZZ99999")),
            entry(2, listOf("PCZZ99997", "pczz99999", "PCZZ-99999", " PCZZ99999 ")),
            entry(3, emptyList())).attachEcvCompatibility(dataset)

        assertEquals(1, results[0].compatibility.size)
        assertTrue(results[1].compatibility.isEmpty())
        assertTrue(results[2].compatibility.isEmpty())
    }

    @Test fun emptyOwnedDatasetDoesNotReuseAnAlreadyAttachedResult() {
        val stale = entry(1, listOf("PCZZ99999")).copy(
            compatibility = listOf(EcvCompatibilityUiRun("PCZZ99999", run("stale", "A"))))
        val result = listOf(stale).attachEcvCompatibility(EcvCompatibilityDataset.EMPTY).single()
        assertTrue(result.compatibility.isEmpty())
        assertEquals(stale.copy(compatibility = emptyList()), result)
    }
}
