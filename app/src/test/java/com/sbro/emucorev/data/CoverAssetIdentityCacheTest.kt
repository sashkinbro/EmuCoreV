package com.sbro.emucorev.data

import org.junit.Assert.*
import org.junit.Test

class CoverAssetIdentityCacheTest {
    private val asset = VitaCoverAsset(42, VitaCoverStyle.THREE_D, "a".repeat(40), "covers/3d/42.webp", "b".repeat(64))

    @Test fun anUnmappedTitleDoesNotForceParsingTheWholeCatalogAtEveryRestart() {
        val storage = mutableMapOf<String, String>()
        CoverAssetIdentityCache("bundle1", storage::get, storage::put).rememberMissing("PCSA00001", asset.style)
        assertTrue(CoverAssetIdentityCache("bundle1", storage::get, storage::put).isKnownMissing("PCSA00001", asset.style))
        assertFalse(CoverAssetIdentityCache("bundle2", storage::get, storage::put).isKnownMissing("PCSA00001", asset.style))
        assertFalse(CoverAssetIdentityCache("bundle1", storage::get, storage::put).isKnownMissing("PCSA00001", VitaCoverStyle.FLAT))
    }

    @Test fun identitiesSurviveRepositoryRecreationWithoutParsingTheCatalog() {
        val storage = mutableMapOf<String, String>()
        val first = CoverAssetIdentityCache("bundle1", storage::get, storage::put)
        first.remember("PCSA00001", asset)
        val restarted = CoverAssetIdentityCache("bundle1", storage::get, storage::put)
        assertEquals(asset, restarted.find("pcsa-00001", VitaCoverStyle.THREE_D))
        assertNull(restarted.find("PCSA00002", VitaCoverStyle.THREE_D))
        assertNull(restarted.find("PCSA00001", VitaCoverStyle.FLAT))
    }

    @Test fun changedCatalogBytesInvalidateOldMappingsEvenWhenRevisionLooksTheSame() {
        val storage = mutableMapOf<String, String>()
        CoverAssetIdentityCache("old-bundle", storage::get, storage::put).remember("PCSA00001", asset)
        assertNull(CoverAssetIdentityCache("new-bundle", storage::get, storage::put).find("PCSA00001", asset.style))
    }

    @Test fun malformedOrCrossSerialMetadataNeverAuthorizesArtwork() {
        val storage = mutableMapOf<String, String>()
        val cache = CoverAssetIdentityCache("bundle", storage::get, storage::put)
        cache.remember("PCSA00001", asset)
        val valid = storage.values.single()
        for (corrupt in listOf("broken", valid.replace("PCSA00001", "PCSA00002"),
            valid.replace("b".repeat(64), "invalid"), valid + "|extra")) {
            storage[storage.keys.single()] = corrupt
            assertNull(cache.find("PCSA00001", asset.style))
        }
    }

    @Test fun unchangedMetadataDoesNotRewritePreferencesOnEveryRead() {
        val storage = mutableMapOf<String, String>()
        var writes = 0
        val cache = CoverAssetIdentityCache("bundle", storage::get) { key, value -> storage[key] = value; writes++ }
        cache.remember("PCSA00001", asset)
        cache.remember("PCSA00001", asset)
        assertEquals(asset, cache.find("PCSA00001", asset.style))
        assertEquals(1, writes)
    }
}
