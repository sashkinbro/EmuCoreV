package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test

class EcvCompatibilityParserTest {
    @Test fun uiRowsPreserveExactTitleIdOrderDistinctIdsAndEveryDeviceDate() {
        val original = EcvCompatibilityParser().parse(twoDeviceFixture)
        val runs = original.runsFor("PCZZ99999")
        val otherId = "PCZY99999"
        val dataset = original.copy(records = original.records + EcvCompatibilityRecord(otherId, listOf(runs[1])))
        val rows = dataset.rowsFor(listOf(otherId, "pczz99999", "PCZZ99999 ", "PCZZ99999", otherId, "PCZZ00000"))
        assertEquals(listOf(otherId, "PCZZ99999", "PCZZ99999"), rows.map { it.titleId })
        assertEquals(listOf(runs[1], runs[0], runs[1]), rows.map { it.run })
        assertEquals(runs[0].device, rows[1].run.device)
        assertEquals(runs[1].testedOn, rows[2].run.testedOn)
        assertTrue(dataset.rowsFor(listOf("pczz99999", " PCZZ99999", "PCZZ00000")).isEmpty())
    }

    @Test fun rejectsPrivateAndroidPathsAndUriReferencesInNotes() {
        for (privateReference in listOf("/data/user/0/app/private", "content://private/item", "file:///private/item")) {
            assertThrows(privateReference, IllegalArgumentException::class.java) {
                EcvCompatibilityParser().parse(statusFixture.replace("synthetic status classification fixture", privateReference))
            }
        }
    }
    @Test
    fun keepsRegionalTitleIdsAndDeviceRunsSeparate() {
        val dataset = EcvCompatibilityParser().parse(twoDeviceFixture)

        val runs = dataset.runsFor("PCZZ99999")
        assertEquals(2, runs.size)
        assertEquals("Fixture Maker Device A", "${runs[0].device.manufacturer} ${runs[0].device.model}")
        assertEquals("Fixture Maker Device B", "${runs[1].device.manufacturer} ${runs[1].device.model}")
        assertEquals(EcvCompatibilityStatus.PASSED, runs[0].results.gameplay.status)
        assertEquals(EcvCompatibilityStatus.LIMITED, runs[0].results.coldLoad.status)
        assertEquals(EcvCompatibilityStatus.PASSED, runs[1].results.gameplay.status)
        assertEquals(EcvCompatibilityStatus.NOT_TESTED, runs[1].results.graphics.status)

        assertTrue(dataset.runsFor("PCZY99999").isEmpty())
        assertTrue(dataset.runsFor("PCZZ99999 ").isEmpty())
        assertTrue(dataset.runsFor(null).isEmpty())
    }

    @Test
    fun preservesPassedFailedLimitedAndNotTestedAsDistinctResults() {
        val run = EcvCompatibilityParser().parse(statusFixture).runsFor("PCZZ99123").single()

        assertEquals(EcvCompatibilityStatus.PASSED, run.results.gameplay.status)
        assertEquals(EcvCompatibilityStatus.FAILED, run.results.graphics.status)
        assertEquals(EcvCompatibilityStatus.LIMITED, run.results.audio.status)
        assertEquals(EcvCompatibilityStatus.NOT_TESTED, run.results.quickSave.status)
        assertEquals(EcvCompatibilityStatus.FAILED, run.results.warmLoad.status)
        assertEquals(EcvCompatibilityStatus.PASSED, run.results.coldLoad.status)
    }

    @Test
    fun rejectsUnsupportedMalformedAmbiguousAndUnprovenDatasetsAsAWhole() {
        val invalidFixtures = listOf(
            twoDeviceFixture.replace("\"schema_version\":1", "\"schema_version\":2"),
            twoDeviceFixture.replace("\"source\":\"EmuCoreV\"", "\"source\":\"Vita3K\""),
            twoDeviceFixture.replace("PCZZ99999", "PCZZ9999"),
            twoDeviceFixture.replace("2026-11-12", "2026-02-30"),
            twoDeviceFixture.replace("\"tested_on\":\"2026-11-12\"", "\"tested_on\":\"2026-11-14\""),
            twoDeviceFixture.replace("\"title_id\":\"PCZZ99999\"", "\"title_id\":\"pczz99999\""),
            twoDeviceFixture.replace("\"source\":\"EmuCoreV\",", "\"source\":\"EmuCoreV\",\"source\":\"EmuCoreV\","),
            twoDeviceFixture.replace("synthetic fixture run A", "https://example.invalid/private"),
            twoDeviceFixture.replace("\"cold_load\":{", "\"unexpected\":true,\"cold_load\":{"),
            twoDeviceFixture.replace("\"status\":\"passed\",\"evidence\":[\"fixture-gameplay-a\"]", "\"status\":\"passed\",\"evidence\":[]"),
            twoDeviceFixture.replace("\"run_id\":\"fixture-device-b-run\"", "\"run_id\":\"fixture-device-a-run\"")
        )

        invalidFixtures.forEach { json ->
            assertThrows(IllegalArgumentException::class.java) {
                EcvCompatibilityParser().parse(json)
            }
        }
    }

    @Test
    fun rejectsDocumentsBeyondTheConfiguredSizeLimit() {
        val oversized = " ".repeat(EcvCompatibilityParser.MAX_DOCUMENT_BYTES + 1)

        assertThrows(IllegalArgumentException::class.java) {
            EcvCompatibilityParser().parse(oversized)
        }
    }

    private companion object {
        val statusFixture = """
            {
              "schema_version":1,
              "dataset_version":1,
              "source":"EmuCoreV",
              "generated_on":"2026-11-13",
              "records":[{
                "title_id":"PCZZ99123",
                "runs":[{
                  "run_id":"status-fixture-1",
                  "device":{"manufacturer":"Fixture Maker","model":"Status Fixture Device","soc":null,"gpu":null},
                  "app_version":"test-app-1",
                  "core_version":"test-core-1",
                  "tested_on":"2026-11-12",
                  "renderer_settings":{},
                  "results":{
                    "gameplay":{"status":"passed","evidence":["fixture-gameplay"]},
                    "graphics":{"status":"failed","evidence":["fixture-graphics"]},
                    "audio":{"status":"limited","evidence":["fixture-audio"]},
                    "quick_save":{"status":"not_tested","evidence":[]},
                    "warm_load":{"status":"failed","evidence":["fixture-warm-load"]},
                    "cold_load":{"status":"passed","evidence":["fixture-cold-load"]}
                  },
                  "notes":"synthetic status classification fixture"
                }]
              }]
            }
        """.trimIndent()

        val twoDeviceFixture = """
            {
              "schema_version":1,
              "dataset_version":7,
              "source":"EmuCoreV",
              "generated_on":"2026-11-13",
              "records":[{
                "title_id":"PCZZ99999",
                "runs":[
                  {
                    "run_id":"fixture-device-a-run",
                    "device":{"manufacturer":"Fixture Maker","model":"Device A","soc":"Synthetic SoC A","gpu":"Synthetic GPU A"},
                    "app_version":"test-app-1",
                    "core_version":"test-core-1",
                    "tested_on":"2026-11-12",
                    "renderer_settings":{"backend":"Vulkan"},
                    "results":{
                      "gameplay":{"status":"passed","evidence":["fixture-gameplay-a"]},
                      "graphics":{"status":"limited","evidence":["fixture-graphics-a"]},
                      "audio":{"status":"not_tested","evidence":[]},
                      "quick_save":{"status":"passed","evidence":["fixture-save-a"]},
                      "warm_load":{"status":"passed","evidence":["fixture-warm-a"]},
                      "cold_load":{"status":"limited","evidence":["fixture-cold-a"]}
                    },
                    "notes":"synthetic fixture run A"
                  },
                  {
                    "run_id":"fixture-device-b-run",
                    "device":{"manufacturer":"Fixture Maker","model":"Device B","soc":"Synthetic SoC B","gpu":"Synthetic GPU B"},
                    "app_version":"test-app-1",
                    "core_version":"test-core-1",
                    "tested_on":"2026-11-12",
                    "renderer_settings":{"backend":"Vulkan"},
                    "results":{
                      "gameplay":{"status":"passed","evidence":["fixture-gameplay-b"]},
                      "graphics":{"status":"not_tested","evidence":[]},
                      "audio":{"status":"not_tested","evidence":[]},
                      "quick_save":{"status":"not_tested","evidence":[]},
                      "warm_load":{"status":"limited","evidence":["fixture-warm-b"]},
                      "cold_load":{"status":"not_tested","evidence":[]}
                    },
                    "notes":"synthetic fixture run B"
                  }
                ]
              }]
            }
        """.trimIndent()
    }
}
