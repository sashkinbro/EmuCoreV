package com.sbro.emucorev.ui.common

import org.junit.Assert.assertEquals
import org.junit.Test

class CoreDisplayBrandingTest {
    private val canonicalCore = "ECV 0.3 (based on Vita3K 0.2.1)"

    @Test fun missingTelemetryStillIdentifiesTheModifiedCore() {
        for (raw in listOf(null, "", " \n\t ")) assertEquals("ECV 0.3", formatCoreDisplayName(raw, canonicalCore))
    }

    @Test fun legacyUpstreamVersionIsNeverMisreportedAsTheModifiedCoreVersion() {
        assertEquals("ECV 0.3", formatCoreDisplayName("Vita3K 0.2.1 (9bd372fab)", canonicalCore))
    }

    @Test fun canonicalNativeForkVersionIsNeverReplacedByTheAppVersion() {
        assertEquals("ECV 0.4-dev+abc123", formatCoreDisplayName("ECV 0.4-dev+abc123", canonicalCore))
    }

    @Test fun bareLegacyBrandDoesNotProduceADuplicateBrand() {
        assertEquals("ECV 0.3", formatCoreDisplayName("Vita3K", canonicalCore))
        assertEquals("ECV 0.3", formatCoreDisplayName("ECV", canonicalCore))
        assertEquals("ECV 0.3", formatCoreDisplayName("ECV (based on Vita3K 0.2.1)", canonicalCore))
    }

    @Test fun alreadyAdaptedAndPaddedSamplesAreIdempotent() {
        val once = formatCoreDisplayName("  ecv  0.3  ", canonicalCore)
        assertEquals("ECV 0.3", once)
        assertEquals(once, formatCoreDisplayName(once, canonicalCore))
    }

    @Test fun versionOnlyTelemetryIsKept() {
        assertEquals("ECV v0.3", formatCoreDisplayName("v0.3", canonicalCore))
    }

    @Test fun upstreamAttributionIsSeparatedFromTheHudVersionWithoutLosingBuildDetails() {
        assertEquals("ECV 0.3 +abc123", formatCoreDisplayName("ECV 0.3 (based on Vita3K 0.2.1) +abc123", canonicalCore))
        assertEquals("Vita3K 0.2.1", upstreamCoreDisplayName(canonicalCore))
    }

    @Test fun distinctBuildNamesAreNotMistakenForTheExactCoreBrand() {
        assertEquals("ECV Vita3KPlus-build7", formatCoreDisplayName("Vita3KPlus-build7", canonicalCore))
    }

    @Test fun missingUpstreamMetadataDoesNotInventAnUpstreamVersion() {
        assertEquals(null, upstreamCoreDisplayName("ECV 0.4-dev"))
        assertEquals("Vita3K 0.2.1", upstreamCoreDisplayName("Vita3K 0.2.1"))
    }

    @Test fun canonicalFallbackFollowsTheProviderWhenTheForkVersionChanges() {
        val future = "ECV 1.7 (based on Vita3K 0.9.2)"
        assertEquals("ECV 1.7", formatCoreDisplayName(null, future))
        assertEquals("ECV 1.7", formatCoreDisplayName("Vita3K 0.9.2", future))
        assertEquals("Vita3K 0.9.2", upstreamCoreDisplayName(future))
    }
}
