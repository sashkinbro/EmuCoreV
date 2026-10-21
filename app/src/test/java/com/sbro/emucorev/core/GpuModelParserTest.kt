package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GpuModelParserTest {
    @Test
    fun `parses Adreno renderer strings`() {
        assertEquals("Adreno 610", GpuModelParser.parse("Adreno (TM) 610")?.displayName)
        assertEquals(GpuVendor.ADRENO, GpuModelParser.parse("Adreno (TM) 610")?.vendor)
        assertEquals("Adreno 640", GpuModelParser.parse("ANGLE (Qualcomm, Adreno (TM) 640, OpenGL ES 3.2)")?.displayName)
        assertEquals("Adreno 830", GpuModelParser.parse("Adreno 830")?.displayName)
        assertEquals("Adreno 505", GpuModelParser.parse("  adreno (tm) 505  ")?.displayName)
    }

    @Test
    fun `parses Mali renderer strings with core counts`() {
        assertEquals("Mali-G52 MC2", GpuModelParser.parse("Mali-G52 MC2")?.displayName)
        assertEquals("Mali-G57 MC2", GpuModelParser.parse("Mali-G57MC2")?.displayName)
        assertEquals("Mali-G52", GpuModelParser.parse("Mali-G52")?.displayName)
        assertEquals("Mali-450", GpuModelParser.parse("Mali-450MP")?.displayName)
        assertEquals("Mali-T880 MP12", GpuModelParser.parse("Mali-T880 MP12")?.displayName)
        assertEquals("Immortalis-G715 MC11", GpuModelParser.parse("Immortalis-G715 MC11")?.displayName)
        assertEquals("Mali-G1 Ultra MC12", GpuModelParser.parse("Mali-G1 Ultra MC12")?.displayName)
        assertEquals("Mali-G2 Ultra NX MP12", GpuModelParser.parse("Mali-G2 Ultra NX MP12")?.displayName)
        assertEquals(GpuVendor.MALI, GpuModelParser.parse("Mali-G68")?.vendor)
    }

    @Test
    fun `parses Xclipse and PowerVR renderer strings`() {
        assertEquals("Xclipse 920", GpuModelParser.parse("Xclipse 920")?.displayName)
        assertEquals(GpuVendor.XCLIPSE, GpuModelParser.parse("Xclipse 940")?.vendor)
        assertEquals("PowerVR GE8320", GpuModelParser.parse("PowerVR Rogue GE8320")?.displayName)
        assertEquals("PowerVR DXT-48-1536", GpuModelParser.parse("PowerVR DXT-48-1536")?.displayName)
        assertEquals("PowerVR CXTP-48-1536", GpuModelParser.parse("PowerVR CXTP-48-1536")?.displayName)
        assertEquals("PowerVR CXTP-48-1536", GpuModelParser.parse("IMG CXTP-48-1536")?.displayName)
        assertEquals("PowerVR", GpuModelParser.parse("Imagination Technologies PowerVR")?.displayName)
        assertEquals("Maleoon 910", GpuModelParser.parse("Maleoon 910")?.displayName)
    }

    @Test
    fun `rejects emulator and unknown renderers`() {
        assertNull(GpuModelParser.parse(null))
        assertNull(GpuModelParser.parse("   "))
        assertNull(GpuModelParser.parse("Adreno (TM)"))
        assertNull(GpuModelParser.parse("Mali"))
        assertNull(GpuModelParser.parse("SwiftShader"))
        assertNull(
            GpuModelParser.parse(
                "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device (Subzero), OpenGL ES 3.1))"
            )
        )
        assertNull(GpuModelParser.parse("Android Emulator OpenGL ES Translator"))
        assertNull(GpuModelParser.parse("Mesa OffScreen"))
    }
}
