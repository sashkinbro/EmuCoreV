package com.sbro.emucorev.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CoverRedirectPolicyTest {
    private val from = "https://raw.githubusercontent.com/sashkinbro/EmuCoreV-Covers/abc/covers/1.webp"

    @Test fun sameHostAbsoluteAndRelativeRedirectsAreFollowed() {
        assertEquals("https://raw.githubusercontent.com/other/path.webp",
            CoverArtRepository.followableRedirect(from, "https://raw.githubusercontent.com/other/path.webp"))
        assertEquals("https://raw.githubusercontent.com/moved/1.webp",
            CoverArtRepository.followableRedirect(from, "/moved/1.webp"))
    }

    @Test fun foreignHostDowngradeCredentialsAndPortAreRefused() {
        assertNull(CoverArtRepository.followableRedirect(from, "https://evil.example/1.webp"))
        assertNull(CoverArtRepository.followableRedirect(from, "http://raw.githubusercontent.com/1.webp"))
        assertNull(CoverArtRepository.followableRedirect(from, "https://u@raw.githubusercontent.com/1.webp"))
        assertNull(CoverArtRepository.followableRedirect(from, "https://raw.githubusercontent.com:8443/1.webp"))
        assertNull(CoverArtRepository.followableRedirect(from, null))
        assertNull(CoverArtRepository.followableRedirect(from, "  "))
    }
}
