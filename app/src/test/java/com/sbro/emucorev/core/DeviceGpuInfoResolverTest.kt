package com.sbro.emucorev.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DeviceGpuInfoResolverTest {
    private fun model(name: String, vendor: GpuVendor = GpuVendor.MALI) =
        GpuModelInfo(vendor, name)

    private fun resolve(renderer: String?, catalogModel: GpuModelInfo?) =
        DeviceGpuInfoResolver.resolve(renderer, "test SoC") { catalogModel }

    @Test
    fun `renderer identity wins over a conflicting catalog`() {
        val raw = "ANGLE (Qualcomm, Adreno (TM) 610, OpenGL ES 3.2)"
        val snapshot = resolve(raw, model("Mali-G57 MC4"))
        assertEquals("Adreno 610", snapshot.model?.displayName)
        assertEquals(GpuInfoSource.GL_RENDERER, snapshot.source)
        assertEquals(raw, snapshot.renderer)
    }

    @Test
    fun `catalog fallback is device identity even when active renderer is software`() {
        listOf(null, "", "SwiftShader", "unrecognized renderer").forEach { raw ->
            val hardware = model("Mali-G57 MP1")
            val snapshot = resolve(raw, hardware)
            assertEquals(hardware, snapshot.model)
            assertEquals(GpuInfoSource.SOC_CATALOG, snapshot.source)
            assertEquals(raw, snapshot.renderer)
        }
    }

    @Test
    fun `missing device evidence remains unknown without inventing a vendor`() {
        val snapshot = resolve("unknown GPU", null)
        assertNull(snapshot.model)
        assertEquals(GpuInfoSource.UNKNOWN, snapshot.source)
        assertEquals("unknown GPU", snapshot.renderer)
    }

    @Test
    fun `only matching Mali base models gain missing catalog shader cores`() {
        assertEquals("Mali-G57 MP1", resolve("ARM, Mali-G57", model("Mali-G57 MP1")).model?.displayName)
        assertEquals("Mali-G57 MC4", resolve("Mali-G57 r1p0", model("mali-g57 mc4")).model?.displayName)
        assertEquals("Mali-G57 MC2", resolve("Mali-G57 MC2", model("Mali-G57 MP1")).model?.displayName)
        assertEquals("Mali-G57", resolve("Mali-G57", model("Mali-G76 MP12")).model?.displayName)
        assertEquals("Mali-G57", resolve("Mali-G57", model("Mali-G57 MP1", GpuVendor.UNKNOWN)).model?.displayName)
        assertEquals("Mali-G57", resolve("Mali-G57", null).model?.displayName)
        assertEquals("Immortalis-G715 MC11", resolve("Immortalis-G715", model("Immortalis-G715 MC11")).model?.displayName)
        assertEquals("Mali-G2 Ultra NX MP12", resolve("Mali-G2 Ultra NX MP12", model("Mali-G2 Ultra NX MC8")).model?.displayName)
    }

    @Test
    fun `execution engines malformed counts and different tiers never merge`() {
        listOf("Mali-G52 2EE", "Mali-G52 MC123", "Mali-G52 MC0", "Mali-G52 MC2 extra").forEach {
            assertEquals("Mali-G52", resolve("Mali-G52", model(it)).model?.displayName)
        }
        assertEquals("Mali-G1", resolve("Mali-G1", model("Mali-G1 Ultra MC12")).model?.displayName)
        assertEquals("Mali-G52 MC1", resolve("Mali-G52 r1p0 2EE MC1", model("Mali-G52 MC2")).model?.displayName)
    }

    @Test
    fun `core enrichment retains renderer metadata rather than copying catalog object`() {
        val catalog = GpuModelInfo(GpuVendor.MALI, "Mali-G57 MC4", AdrenoFamily.A7XX)
        val snapshot = resolve("Mali-G57", catalog)
        assertEquals("Mali-G57 MC4", snapshot.model?.displayName)
        assertNull(snapshot.model?.adrenoFamily)
        assertEquals(GpuInfoSource.GL_RENDERER, snapshot.source)
    }

    @Test
    fun `active native identity never inherits catalog hardware or shader cores`() {
        val active = DeviceGpuInfoResolver.resolveActiveRenderer("Mali-G57")
        assertEquals("Mali-G57", active.model?.displayName)
        assertEquals(GpuInfoSource.NATIVE_RENDERER, active.source)
        assertEquals("Mali-G57", active.renderer)
    }

    @Test
    fun `active software and unknown native strings are retained without hardware claims`() {
        listOf(null, "", "SwiftShader", "llvmpipe", "unknown native GPU").forEach { raw ->
            val active = DeviceGpuInfoResolver.resolveActiveRenderer(raw)
            assertNull(active.model)
            assertEquals(GpuInfoSource.UNKNOWN, active.source)
            assertEquals(raw, active.renderer)
        }
    }

    @Test
    fun `catalog receives original SoC hint and supports missing hints`() {
        var captured: String? = "not called"
        val snapshot = DeviceGpuInfoResolver.resolve(null, null) { soc ->
            captured = soc
            null
        }
        assertNull(captured)
        assertEquals(GpuInfoSource.UNKNOWN, snapshot.source)
    }

    @Test
    fun `default catalog adapter uses the unchanged V catalog`() {
        val soc = "Unisoc T606/T612"
        val snapshot = DeviceGpuInfoResolver.resolve(null, soc)
        assertEquals(SocGpuCatalog.forSoc(soc), snapshot.model)
        assertEquals(GpuInfoSource.SOC_CATALOG, snapshot.source)
    }
}
