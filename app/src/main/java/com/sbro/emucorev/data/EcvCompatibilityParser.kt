package com.sbro.emucorev.data

import java.nio.ByteBuffer
import java.nio.charset.CodingErrorAction
import java.time.LocalDate
import java.time.format.DateTimeParseException
import java.util.ArrayDeque
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.intOrNull
import kotlinx.serialization.json.jsonPrimitive

/** Strict parser for the locally owned ECV compatibility format. */
class EcvCompatibilityParser {
    fun parse(json: String): EcvCompatibilityDataset {
        val bytes = json.toByteArray(Charsets.UTF_8)
        require(bytes.size <= MAX_DOCUMENT_BYTES) { "ECV compatibility document exceeds the size limit" }
        requireNoDuplicateObjectKeys(json)
        val root = runCatching { Json.parseToJsonElement(json) }
            .getOrElse { throw IllegalArgumentException("Invalid ECV compatibility JSON", it) }
            .asObject("root")

        root.requireKeys(
            path = "root",
            required = setOf("schema_version", "dataset_version", "source", "generated_on", "records")
        )
        val schemaVersion = root.requiredInt("schema_version", "root")
        require(schemaVersion == SUPPORTED_SCHEMA_VERSION) {
            "Unsupported ECV compatibility schema version: $schemaVersion"
        }
        val datasetVersion = root.requiredInt("dataset_version", "root")
        require(datasetVersion > 0) { "dataset_version must be positive" }
        val source = root.requiredString("source", "root", maxLength = 40)
        require(source == SUPPORTED_SOURCE) { "Unsupported ECV compatibility source: $source" }
        val generatedOn = root.requiredDate("generated_on", "root")
        val recordArray = root.requiredArray("records", "root")
        require(recordArray.size <= MAX_RECORDS) { "Too many ECV compatibility records" }

        val seenTitleIds = mutableSetOf<String>()
        val seenRunIds = mutableSetOf<String>()
        var totalRuns = 0
        val records = recordArray.mapIndexed { index, element ->
            val path = "records[$index]"
            val record = element.asObject(path)
            record.requireKeys(path, required = setOf("title_id", "runs"))
            val titleId = record.requiredString(path = path, key = "title_id", maxLength = 9)
            require(TITLE_ID_PATTERN.matches(titleId)) { "$path.title_id is not a supported Vita Title ID" }
            require(seenTitleIds.add(titleId)) { "Duplicate Title ID: $titleId" }

            val runsArray = record.requiredArray("runs", path)
            require(runsArray.isNotEmpty()) { "$path.runs must contain at least one run" }
            require(runsArray.size <= MAX_RUNS_PER_TITLE_ID) { "$path has too many runs" }
            totalRuns += runsArray.size
            require(totalRuns <= MAX_RUNS) { "Too many ECV compatibility runs" }
            val runs = runsArray.mapIndexed { runIndex, runElement ->
                val runPath = "$path.runs[$runIndex]"
                parseRun(
                    runElement = runElement,
                    path = runPath,
                    generatedOn = generatedOn,
                    seenRunIds = seenRunIds
                )
            }
            EcvCompatibilityRecord(titleId = titleId, runs = runs)
        }

        return EcvCompatibilityDataset(
            schemaVersion = schemaVersion,
            datasetVersion = datasetVersion,
            source = source,
            generatedOn = generatedOn,
            records = records
        )
    }

    fun parse(bytes: ByteArray): EcvCompatibilityDataset {
        require(bytes.size <= MAX_DOCUMENT_BYTES) { "ECV compatibility document exceeds the size limit" }
        val decoder = Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
        val text = runCatching { decoder.decode(ByteBuffer.wrap(bytes)).toString() }
            .getOrElse { throw IllegalArgumentException("ECV compatibility document is not valid UTF-8", it) }
        return parse(text)
    }

    private fun parseRun(
        runElement: JsonElement,
        path: String,
        generatedOn: LocalDate,
        seenRunIds: MutableSet<String>
    ): EcvCompatibilityRun {
        val run = runElement.asObject(path)
        run.requireKeys(
            path = path,
            required = setOf(
                "run_id", "device", "app_version", "core_version", "tested_on",
                "renderer_settings", "results"
            ),
            optional = setOf("notes")
        )
        val runId = run.requiredString("run_id", path, maxLength = MAX_RUN_ID_LENGTH)
        require(RUN_ID_PATTERN.matches(runId)) { "$path.run_id contains unsupported characters" }
        require(seenRunIds.add(runId)) { "Duplicate ECV run ID: $runId" }

        val deviceObject = run.requiredObject("device", path)
        deviceObject.requireKeys(
            path = "$path.device",
            required = setOf("manufacturer", "model"),
            optional = setOf("soc", "gpu")
        )
        val device = EcvCompatibilityDevice(
            manufacturer = deviceObject.requiredString("manufacturer", "$path.device", MAX_DEVICE_FIELD_LENGTH),
            model = deviceObject.requiredString("model", "$path.device", MAX_DEVICE_FIELD_LENGTH),
            soc = deviceObject.optionalString("soc", "$path.device", MAX_DEVICE_FIELD_LENGTH),
            gpu = deviceObject.optionalString("gpu", "$path.device", MAX_DEVICE_FIELD_LENGTH)
        )
        val appVersion = run.requiredString("app_version", path, MAX_VERSION_LENGTH)
        val coreVersion = run.requiredString("core_version", path, MAX_VERSION_LENGTH)
        val testedOn = run.requiredDate("tested_on", path)
        require(!testedOn.isAfter(generatedOn)) { "$path.tested_on is after generated_on" }
        val rendererSettings = parseRendererSettings(run.requiredObject("renderer_settings", path), "$path.renderer_settings")
        val results = parseResults(run.requiredObject("results", path), "$path.results")
        val notes = run.optionalString("notes", path, MAX_NOTES_LENGTH)

        return EcvCompatibilityRun(
            runId = runId,
            device = device,
            appVersion = appVersion,
            coreVersion = coreVersion,
            testedOn = testedOn,
            rendererSettings = rendererSettings,
            results = results,
            notes = notes
        )
    }

    private fun parseRendererSettings(settings: JsonObject, path: String): Map<String, String> {
        require(settings.size <= MAX_RENDERER_SETTINGS) { "$path contains too many settings" }
        return settings.mapValues { (key, value) ->
            require(RENDERER_KEY_PATTERN.matches(key)) { "$path has an invalid key" }
            value.asSafeString("$path.$key", MAX_RENDERER_VALUE_LENGTH)
        }
    }

    private fun parseResults(results: JsonObject, path: String): EcvCompatibilityResults {
        val resultKeys = setOf("gameplay", "graphics", "audio", "quick_save", "warm_load", "cold_load")
        results.requireKeys(path, required = resultKeys)
        return EcvCompatibilityResults(
            gameplay = results.requiredResult("gameplay", path),
            graphics = results.requiredResult("graphics", path),
            audio = results.requiredResult("audio", path),
            quickSave = results.requiredResult("quick_save", path),
            warmLoad = results.requiredResult("warm_load", path),
            coldLoad = results.requiredResult("cold_load", path)
        )
    }

    private fun JsonObject.requiredResult(key: String, path: String): EcvCompatibilityResult {
        val resultPath = "$path.$key"
        val result = getValue(key).asObject(resultPath)
        result.requireKeys(resultPath, required = setOf("status", "evidence"))
        val statusName = result.requiredString("status", resultPath, maxLength = 20)
        val status = STATUS_BY_NAME[statusName]
            ?: throw IllegalArgumentException("$resultPath.status is unsupported")
        val evidence = result.requiredArray("evidence", resultPath)
        require(evidence.size <= MAX_EVIDENCE_PER_RESULT) { "$resultPath has too many evidence references" }
        val evidenceReferences = evidence.mapIndexed { index, item ->
            val reference = item.asSafeString("$resultPath.evidence[$index]", MAX_EVIDENCE_LENGTH)
            require(EVIDENCE_PATTERN.matches(reference)) { "$resultPath contains a non-public evidence reference" }
            reference
        }
        require(evidenceReferences.distinct().size == evidenceReferences.size) { "$resultPath has duplicate evidence references" }
        if (status == EcvCompatibilityStatus.NOT_TESTED) {
            require(evidenceReferences.isEmpty()) { "$resultPath not_tested result cannot have evidence" }
        } else {
            require(evidenceReferences.isNotEmpty()) { "$resultPath observed result requires evidence" }
        }
        return EcvCompatibilityResult(status = status, evidence = evidenceReferences)
    }

    private fun JsonObject.requiredString(key: String, path: String, maxLength: Int): String =
        getValue(key).asSafeString("$path.$key", maxLength)

    private fun JsonObject.optionalString(key: String, path: String, maxLength: Int): String? {
        val element = this[key] ?: return null
        if (element === JsonNull) return null
        return element.asSafeString("$path.$key", maxLength)
    }

    private fun JsonObject.requiredInt(key: String, path: String): Int {
        val primitive = getValue(key) as? JsonPrimitive
            ?: throw IllegalArgumentException("$path.$key must be an integer")
        require(!primitive.isString) { "$path.$key must be an integer" }
        return primitive.intOrNull ?: throw IllegalArgumentException("$path.$key must be an integer")
    }

    private fun JsonObject.requiredDate(key: String, path: String): LocalDate {
        val value = requiredString(key, path, DATE_LENGTH)
        require(DATE_PATTERN.matches(value)) { "$path.$key must use YYYY-MM-DD" }
        return try {
            LocalDate.parse(value).also { date ->
                require(date.toString() == value) { "$path.$key is not canonical" }
            }
        } catch (error: DateTimeParseException) {
            throw IllegalArgumentException("$path.$key is not a valid date", error)
        }
    }

    private fun JsonObject.requiredArray(key: String, path: String): JsonArray =
        getValue(key) as? JsonArray ?: throw IllegalArgumentException("$path.$key must be an array")

    private fun JsonObject.requiredObject(key: String, path: String): JsonObject =
        getValue(key).asObject("$path.$key")

    private fun JsonElement.asObject(path: String): JsonObject =
        this as? JsonObject ?: throw IllegalArgumentException("$path must be an object")

    private fun JsonElement.asSafeString(path: String, maxLength: Int): String {
        val primitive = this as? JsonPrimitive
            ?: throw IllegalArgumentException("$path must be a string")
        require(primitive.isString) { "$path must be a string" }
        val value = primitive.content
        require(value.isNotBlank() && value == value.trim()) { "$path must be non-blank and trimmed" }
        require(value.length <= maxLength) { "$path exceeds the length limit" }
        require(value.none { character -> Character.isISOControl(character) }) { "$path contains control characters" }
        require(!SENSITIVE_REFERENCE_PATTERN.containsMatchIn(value)) { "$path must not contain a private path, URL, or email" }
        return value
    }

    private fun JsonObject.requireKeys(path: String, required: Set<String>, optional: Set<String> = emptySet()) {
        require(keys.containsAll(required)) { "$path is missing required fields" }
        require(keys.all { it in required || it in optional }) { "$path contains unsupported fields" }
    }

    private fun requireNoDuplicateObjectKeys(json: String) {
        data class ScanFrame(
            val isObject: Boolean,
            val keys: MutableSet<String> = mutableSetOf(),
            var expectsKey: Boolean = true
        )
        val frames = ArrayDeque<ScanFrame>()
        var index = 0
        while (index < json.length) {
            when (val character = json[index]) {
                '"' -> {
                    val end = scanStringEnd(json, index)
                    val current = frames.peekLast()
                    if (current != null && current.isObject && current.expectsKey) {
                        val key = runCatching { Json.parseToJsonElement(json.substring(index, end + 1)).jsonPrimitive.content }
                            .getOrElse { throw IllegalArgumentException("Invalid JSON object key", it) }
                        require(current.keys.add(key)) { "Duplicate JSON object key: $key" }
                        current.expectsKey = false
                    }
                    index = end + 1
                }
                '{' -> {
                    frames.addLast(ScanFrame(isObject = true))
                    index++
                }
                '[' -> {
                    frames.addLast(ScanFrame(isObject = false))
                    index++
                }
                '}' -> {
                    if (frames.isNotEmpty()) frames.removeLast()
                    index++
                }
                ']' -> {
                    if (frames.isNotEmpty()) frames.removeLast()
                    index++
                }
                ',' -> {
                    frames.peekLast()?.takeIf { frame -> frame.isObject }?.expectsKey = true
                    index++
                }
                else -> {
                    index++
                }
            }
        }
    }

    private fun scanStringEnd(json: String, start: Int): Int {
        var index = start + 1
        while (index < json.length) {
            when (json[index]) {
                '\\' -> index += 2
                '"' -> return index
                else -> index++
            }
        }
        return json.length - 1
    }

    companion object {
        const val MAX_DOCUMENT_BYTES = 2 * 1024 * 1024
        const val SUPPORTED_SCHEMA_VERSION = 1
        const val SUPPORTED_SOURCE = "EmuCoreV"
        const val MAX_RECORDS = 5_000
        const val MAX_RUNS = 10_000
        const val MAX_RUNS_PER_TITLE_ID = 100

        val TITLE_ID_PATTERN = Regex("(?:PC[A-Z]{2}[0-9]{5}|NPXS[0-9]{5})")
        private val RUN_ID_PATTERN = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,119}")
        private val DATE_PATTERN = Regex("[0-9]{4}-[0-9]{2}-[0-9]{2}")
        private val RENDERER_KEY_PATTERN = Regex("[a-z][a-z0-9_.-]{0,39}")
        private val EVIDENCE_PATTERN = Regex("[A-Za-z0-9][A-Za-z0-9._-]{0,119}")
        private val SENSITIVE_REFERENCE_PATTERN = Regex(
            "(?i)(https?://|content://|file://|www\\.|[A-Z]:[\\\\/]|/(?:sdcard|storage|home|users|data|mnt)/|[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,})"
        )
        private const val DATE_LENGTH = 10
        private const val MAX_DEVICE_FIELD_LENGTH = 120
        private const val MAX_VERSION_LENGTH = 80
        private const val MAX_RUN_ID_LENGTH = 120
        private const val MAX_NOTES_LENGTH = 500
        private const val MAX_RENDERER_SETTINGS = 32
        private const val MAX_RENDERER_VALUE_LENGTH = 120
        private const val MAX_EVIDENCE_PER_RESULT = 20
        private const val MAX_EVIDENCE_LENGTH = 120
        private val STATUS_BY_NAME = EcvCompatibilityStatus.entries.associateBy { status ->
            when (status) {
                EcvCompatibilityStatus.PASSED -> "passed"
                EcvCompatibilityStatus.FAILED -> "failed"
                EcvCompatibilityStatus.LIMITED -> "limited"
                EcvCompatibilityStatus.NOT_TESTED -> "not_tested"
            }
        }
    }
}
