package com.sbro.emucorev.data

import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.json.JSONObject

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class VitaCoverIndexTest {
    private val revision = "a".repeat(40)
    private fun fixture(): JSONObject = JSONObject("""{
      "schema_version":2,"asset_revision":"$revision",
      "games":{
        "42":{"path":"covers/42.webp","path_3d":"covers/3d/42.webp","sha256":"${"1".repeat(64)}","sha256_3d":"${"2".repeat(64)}","source_image_id":"coABC","source_url":"https://images.igdb.com/igdb/image/upload/t_original/coABC.jpg"},
        "43":{"path":"covers/43.webp","path_3d":"covers/3d/43.webp","sha256":"${"3".repeat(64)}","sha256_3d":"${"4".repeat(64)}","source_image_id":"coDEF","source_url":"https://images.igdb.com/igdb/image/upload/t_original/coDEF.jpg"}
      },"serials":{"PCSA00001":"42"}
    }""")
    private fun index(json: JSONObject = fixture()) = VitaCoverIndex.fromJson(json.toString())

    @Test fun exactIdentityAndStyles() {
        val cover = index().find("pcsa-00001")
        assertNotNull(cover)
        assertEquals(42L, cover!!.igdbId)
        assertEquals("https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/$revision/covers/3d/42.webp", cover.url)
        val flat = index().find(igdbId = 42, style = VitaCoverStyle.FLAT)!!
        assertNotEquals(cover.cacheKey, flat.cacheKey)
    }
    @Test fun unknownOrMalformedSuppliedSerialAlwaysFailsClosed() {
        assertNull(index().find("PCSA99999", 42))
        assertNull(index().find("", 42))
        assertNull(index().find("../PCSA00001", 42))
        assertNull(index().find("PCSA00001 suffix", 42))
    }
    @Test fun conflictingIdsAndLegacyImageFailClosed() {
        assertNull(index().find("PCSA00001", 43))
        assertNull(index().find(igdbId = 0))
        assertNull(index().find("PCSA00001", candidateUrl = "https://images.igdb.com/igdb/image/upload/t_cover_big/coDEF.jpg"))
        assertNull(index().find(igdbId = 43, candidateUrl = "https://images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg"))
    }
    @Test fun uniqueLegacyImageAndPinnedRepoUrlResolve() {
        assertEquals(42L, index().find(candidateUrl = "https://images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg")!!.igdbId)
        assertEquals(42L, index().find(candidateUrl = "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/$revision/covers/42.webp")!!.igdbId)
        assertNull(index().find(candidateUrl = "https://images.igdb.com.evil/igdb/image/upload/t_cover_big/coABC.jpg"))
        assertNull(index().find(candidateUrl = "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/main/covers/42.webp"))
    }
    @Test fun ambiguousImageIdIsNeverUsedForIdentity() {
        val json = fixture()
        json.getJSONObject("games").getJSONObject("43").put("source_image_id", "coABC")
            .put("source_url", "https://images.igdb.com/igdb/image/upload/t_original/coABC.jpg")
        assertNull(index(json).find(candidateUrl = "https://images.igdb.com/igdb/image/upload/t_cover_big/coABC.jpg"))
        assertEquals(42L, index(json).find("PCSA00001")!!.igdbId)
    }
    @Test fun invalidRevisionSchemaAndRecordAreRejected() {
        for (value in listOf("main", "../bad", "f".repeat(39))) {
            assertNull(index(fixture().put("asset_revision", value)).find(igdbId = 42))
        }
        assertNull(index(fixture().put("schema_version", 1)).find(igdbId = 42))
        for ((key, value) in listOf("path" to "covers/../42.webp", "path_3d" to "covers/3d/43.webp",
            "sha256" to "missing", "source_url" to "https://evil.test/coABC.jpg")) {
            val json = fixture()
            json.getJSONObject("games").getJSONObject("42").put(key, value)
            assertNull(index(json).find(igdbId = 42))
        }
    }
    @Test fun conflictingNormalizedSerialsRejectBothMappings() {
        val json = fixture()
        json.getJSONObject("serials").put("pcsa-00001", "43")
        assertNull(index(json).find("PCSA00001"))
    }
}
