package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class TitleIdsTest {
    @Test fun retailAndHomebrewIdsAreAccepted() {
        listOf("PCSE00001", "PCSB00123", "NPXS10008", "VITASHELL", "SKGD3PLG0", "__VITA_NATIVE_LIBRARY__",
            "__VITA_NATIVE_TROPHIES__").forEach { assertTrue(it, TitleIds.isValid(it)) }
    }

    @Test fun pathTraversalAndSeparatorsAreRejected() {
        listOf("", " ", "..", ".", "../x", "a/b", "a\b", "PCSE00001/../../x", "PCSE00001.xml", "A\u0000B",
            "ABC DEF", "A".repeat(33), "ID\n").forEach { assertFalse(it, TitleIds.isValid(it)) }
        assertFalse(TitleIds.isValid(null))
    }

    @Test fun sanitizeTrimsAndRejects() {
        assertEquals("PCSE00001", TitleIds.sanitizeOrNull("  PCSE00001 "))
        assertNull(TitleIds.sanitizeOrNull("../PCSE00001"))
        assertNull(TitleIds.sanitizeOrNull(null))
    }
}
