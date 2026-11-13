package com.sbro.emucorev.ui.common

import com.sbro.emucorev.R
import com.sbro.emucorev.data.*
import com.sbro.emucorev.ui.detail.detailCompatibilityRows
import java.time.LocalDate
import org.junit.Assert.*
import org.junit.Test

class EcvCompatibilityUiTest {
    private val passed = EcvCompatibilityResult(EcvCompatibilityStatus.PASSED, listOf("Reviewed evidence"))
    private val failed = EcvCompatibilityResult(EcvCompatibilityStatus.FAILED, emptyList())
    private val limited = EcvCompatibilityResult(EcvCompatibilityStatus.LIMITED, emptyList())
    private val untested = EcvCompatibilityResult(EcvCompatibilityStatus.NOT_TESTED, emptyList())
    private fun run(id: String = "run-1") = EcvCompatibilityRun(
        id, EcvCompatibilityDevice("Maker", "Model", "CPU", "GPU"), "1.0", "core-build-123",
        LocalDate.of(2026, 11, 13), mapOf("renderer" to "Vulkan"),
        EcvCompatibilityResults(passed, failed, limited, untested, failed, passed), "Reviewed notes"
    )
    private val dataset = EcvCompatibilityDataset(1, 1, "ecv", LocalDate.of(2026, 11, 13), listOf(
        EcvCompatibilityRecord("PCSE00001", listOf(run(), run("run-2"))),
        EcvCompatibilityRecord("PCSB00001", listOf(run("eu-run")))
    ))

    @Test fun installedUnknownIdNeverFallsThroughToCatalogRegion() {
        assertTrue(detailCompatibilityRows(dataset, "PCSE99999", listOf("PCSB00001")).isEmpty())
        assertTrue(detailCompatibilityRows(dataset, "pcse00001", listOf("PCSE00001")).isEmpty())
        assertTrue(detailCompatibilityRows(dataset, "", listOf("PCSE00001")).isEmpty())
    }

    @Test fun installedIdKeepsOnlyItsOwnRunsDespiteCatalogSerials() {
        val rows = detailCompatibilityRows(dataset, "PCSE00001", listOf("PCSB00001"))
        assertEquals(listOf("PCSE00001", "PCSE00001"), rows.map { it.titleId })
        assertEquals(listOf("run-1", "run-2"), rows.map { it.run.runId })
    }

    @Test fun catalogRowsRetainExactTitleIdAndRunOrderWithoutDuplicateSerials() {
        val rows = detailCompatibilityRows(dataset, null, listOf("PCSB00001", "PCSE00001", "PCSB00001", "PCSE99999"))
        assertEquals(listOf("PCSB00001", "PCSE00001", "PCSE00001"), rows.map { it.titleId })
        assertEquals(listOf("eu-run", "run-1", "run-2"), rows.map { it.run.runId })
    }

    @Test fun sixOutcomesPreserveTheirOwnResultsAndEvidence() {
        val rows = ecvOutcomeRows(EcvCompatibilityUiRun("PCSE00001", run()))
        assertEquals(listOf(R.string.ecv_compatibility_gameplay, R.string.ecv_compatibility_graphics,
            R.string.ecv_compatibility_audio, R.string.ecv_compatibility_quick_save,
            R.string.ecv_compatibility_warm_load, R.string.ecv_compatibility_cold_load), rows.map { it.labelRes })
        assertEquals(listOf(passed, failed, limited, untested, failed, passed), rows.map { it.result })
        assertEquals(listOf("Reviewed evidence"), rows.first().result.evidence)
    }

    @Test fun onlyPassedHasSuccessRoleAndLimitedOrUnknownNeverHaveCheckmark() {
        val pass = ecvOutcomePresentation(EcvCompatibilityStatus.PASSED)
        assertEquals(EcvOutcomeRole.SUCCESS, pass.role)
        assertEquals(R.string.ecv_compatibility_passed, pass.labelRes)
        assertEquals(EcvOutcomeRole.ERROR, ecvOutcomePresentation(EcvCompatibilityStatus.FAILED).role)
        assertEquals(EcvOutcomeRole.WARNING, ecvOutcomePresentation(EcvCompatibilityStatus.LIMITED).role)
        assertEquals(EcvOutcomeRole.NEUTRAL, ecvOutcomePresentation(EcvCompatibilityStatus.NOT_TESTED).role)
        EcvCompatibilityStatus.entries.filter { it != EcvCompatibilityStatus.PASSED }.forEach {
            assertNotEquals(pass.icon, ecvOutcomePresentation(it).icon)
            assertNotEquals(EcvOutcomeRole.SUCCESS, ecvOutcomePresentation(it).role)
        }
    }
}
