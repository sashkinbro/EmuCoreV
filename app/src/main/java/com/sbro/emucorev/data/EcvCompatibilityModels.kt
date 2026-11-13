package com.sbro.emucorev.data

import java.time.LocalDate

enum class EcvCompatibilityStatus {
    PASSED,
    FAILED,
    LIMITED,
    NOT_TESTED
}

/** A run belongs only to this exact regional Title ID, device and date. */
data class EcvCompatibilityUiRun(val titleId: String, val run: EcvCompatibilityRun)

data class EcvCompatibilityDataset(
    val schemaVersion: Int,
    val datasetVersion: Int,
    val source: String,
    val generatedOn: LocalDate,
    val records: List<EcvCompatibilityRecord>
) {
    private val recordsByTitleId: Map<String, EcvCompatibilityRecord> =
        records.associateBy(EcvCompatibilityRecord::titleId)

    /** Returns only runs for the exact, case-sensitive Vita Title ID. */
    fun runsFor(titleId: String?): List<EcvCompatibilityRun> {
        if (titleId == null || !EcvCompatibilityParser.TITLE_ID_PATTERN.matches(titleId)) return emptyList()
        return recordsByTitleId[titleId]?.runs.orEmpty()
    }

    /** No trim/case/name/regional normalization; preserve caller ID order and stored run order. */
    fun rowsFor(titleIds: Iterable<String>): List<EcvCompatibilityUiRun> = titleIds
        .filter { EcvCompatibilityParser.TITLE_ID_PATTERN.matches(it) }
        .distinct()
        .flatMap { titleId -> runsFor(titleId).map { EcvCompatibilityUiRun(titleId, it) } }

    companion object {
        val EMPTY = EcvCompatibilityDataset(
            schemaVersion = EcvCompatibilityParser.SUPPORTED_SCHEMA_VERSION,
            datasetVersion = 0,
            source = EcvCompatibilityParser.SUPPORTED_SOURCE,
            generatedOn = LocalDate.of(1970, 1, 1),
            records = emptyList()
        )
    }
}

data class EcvCompatibilityRecord(
    val titleId: String,
    val runs: List<EcvCompatibilityRun>
)

data class EcvCompatibilityRun(
    val runId: String,
    val device: EcvCompatibilityDevice,
    val appVersion: String,
    val coreVersion: String,
    val testedOn: LocalDate,
    val rendererSettings: Map<String, String>,
    val results: EcvCompatibilityResults,
    val notes: String?
)

data class EcvCompatibilityDevice(
    val manufacturer: String,
    val model: String,
    val soc: String?,
    val gpu: String?
)

data class EcvCompatibilityResult(
    val status: EcvCompatibilityStatus,
    val evidence: List<String>
)

data class EcvCompatibilityResults(
    val gameplay: EcvCompatibilityResult,
    val graphics: EcvCompatibilityResult,
    val audio: EcvCompatibilityResult,
    val quickSave: EcvCompatibilityResult,
    val warmLoad: EcvCompatibilityResult,
    val coldLoad: EcvCompatibilityResult
)
