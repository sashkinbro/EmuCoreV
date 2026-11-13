package com.sbro.emucorev.data

import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class ShareableGameCoverPolicyTest {
    @Test fun unknownSerialNeverReconstructsAnOldManagedOrTitleMatchedUrl() = runBlocking {
        var queried: String? = null
        val result = shareableGameCoverPath("https://images.igdb.com/igdb/image/upload/t_cover_big/co1234.jpg", "PCSE99999") { serial, _ ->
            queried = serial
            null
        }
        assertEquals("PCSE99999", queried)
        assertNull(result)
    }

    @Test fun oldManagedUrlIsReplacedByTheExactSerialsCurrentCover() = runBlocking {
        val result = shareableGameCoverPath("https://images.igdb.com/igdb/image/upload/t_cover_big/co1234.jpg", " PCSE00001 ") { serial, candidate ->
            assertEquals("PCSE00001", serial)
            assertTrue(candidate!!.contains("co1234"))
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/verified/covers/1.webp"
        }
        assertTrue(result!!.endsWith("covers/1.webp"))
    }

    @Test fun explicitCustomWebArtworkRemainsShareable() = runBlocking {
        val result = shareableGameCoverPath(" https://example.com/custom.jpg ", "PCSE00001") { _, _ ->
            fail("Custom web artwork must not be replaced")
            null
        }
        assertEquals("https://example.com/custom.jpg", result)
    }

    @Test fun localCustomArtworkIsNotPublishedAsADevicePath() = runBlocking {
        val result = shareableGameCoverPath("content://art/private-cover", "PCSE00001") { _, candidate ->
            assertEquals("content://art/private-cover", candidate)
            null
        }
        assertNull(result)
    }

    @Test fun absentIdentityDoesNotTriggerAGuess() = runBlocking {
        assertNull(shareableGameCoverPath(null, " ") { _, _ ->
            fail("No identity must not resolve")
            null
        })
    }
}
