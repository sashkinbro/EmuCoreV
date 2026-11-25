package com.sbro.emucorev.data.hub

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HubLocaleResolverTest {
    @Test
    fun everyPublishedHubLocaleIsSupported() {
        assertEquals(18, HubLocaleResolver.supportedLocales.size)
        assertTrue(
            HubLocaleResolver.supportedLocales.containsAll(
                setOf("ar", "cs", "de", "en", "es", "fa", "fr", "hi", "id", "it", "ja", "ko", "pl", "pt", "ru", "tr", "uk", "zh")
            )
        )
    }

    @Test
    fun regionalLanguageTagsFallBackToTheirBaseLocale() {
        assertEquals("uk", HubLocaleResolver.resolve("uk-UA"))
        assertEquals("pt", HubLocaleResolver.resolve("pt-BR"))
        assertEquals("zh", HubLocaleResolver.resolve("zh-Hant-TW"))
    }

    @Test
    fun legacyIndonesianCodeMapsToBcp47Id() {
        assertEquals("id", HubLocaleResolver.resolve("in-ID"))
    }

    @Test
    fun unsupportedLanguageFallsBackToEnglish() {
        assertEquals("en", HubLocaleResolver.resolve("nl-NL"))
        assertEquals("en", HubLocaleResolver.resolve(null))
    }

    @Test
    fun manifestAvailabilityIsRespected() {
        assertEquals("en", HubLocaleResolver.resolve("uk-UA", available = listOf("en", "de")))
        assertEquals("de", HubLocaleResolver.resolve("de-AT", available = listOf("en", "de")))
    }
}
