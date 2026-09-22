package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** Source contracts for upstream Vita3K/Plus fixes ported into the vendored core. */
class PortedUpstreamFixesContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private fun native(path: String) = app.resolve("src/main/cpp/vita3k/vita3k/$path").readText()

    @Test fun multipleImmediateContextsAreTrackedByAddress() {
        val state = native("gxm/include/gxm/state.h")
        assertTrue(state.contains("std::unordered_map<SceGxmContext *, Address> immediate_contexts"))
        assertTrue(state.contains("Address last_immediate_context = 0"))
        assertFalse(state.contains("Address immediate_context = 0"))

        val gxm = native("modules/SceGxm/SceGxm.cpp")
        assertFalse(gxm.contains("SCE_GXM_ERROR_ALREADY_INITIALIZED"))
        assertTrue(gxm.contains("emuenv.gxm.immediate_contexts.emplace(ctx, context->address())"))
        assertTrue(gxm.contains("emuenv.gxm.immediate_contexts.begin(); immediate_context != emuenv.gxm.immediate_contexts.end();"))
    }

    @Test fun plainNidExportOwnershipSurvivesRedirects() {
        val state = native("kernel/include/kernel/state.h")
        assertTrue(state.contains("typedef unordered_map_fast<uint32_t, uint32_t> ExportNidOwners"))
        assertTrue(state.contains("ExportNidOwners export_nid_owners"))

        val loadSelf = native("kernel/src/load_self.cpp")
        assertTrue(loadSelf.contains("kernel.export_nid_owners.insert_or_assign(nid, library_nid)"))
        assertTrue(loadSelf.contains("static bool stub_targets(const uint32_t *stub, Address address)"))
        assertTrue(loadSelf.contains("owner_it->second == library_nid"))

        val taihen = native("modules/taiHEN/taiHEN.cpp")
        assertTrue(taihen.contains("kernel.export_nid_owners.erase(nid)"))
    }

    @Test fun guestThreadStateIsReleasedBeforeProcessExitWakes() {
        val kernel = native("kernel/src/kernel.cpp")
        val threadFn = kernel.substringAfter("static int SDLCALL thread_function")
        assertTrue(threadFn.contains("ThreadStatePtr thread = params.kernel->get_thread"))
        val reset = threadFn.indexOf("thread.reset()")
        val erase = threadFn.indexOf("params.kernel->threads.erase(id)")
        assertTrue(reset in 1 until erase)
    }

    @Test fun libLocationImplementsDisgaeaSaveLoadPath() {
        val location = native("modules/SceLocation/SceLibLocation.cpp")
        assertTrue(location.contains("sceLocationGetPermission"))
        assertTrue(location.contains("confirm_required = 0"))
        assertTrue(location.contains("static bool writable(const MemState &mem, const Ptr<T> &p)"))
        assertTrue(location.contains("Ptr<SceLocationHandle> handle"))
    }

    @Test fun npMatching2ContextEventsAreDispatchedWithoutServer() {
        val state = native("np/include/np/state.h")
        assertTrue(state.contains("struct NpMatching2State"))
        assertTrue(state.contains("std::vector<SceNpMatching2ContextEvent> pending"))

        val matching = native("modules/SceNpMatching2/SceNpMatching2.cpp")
        assertTrue(matching.contains("matching2.pending.push_back({ ctxId, SCE_NP_MATCHING2_CONTEXT_EVENT_START"))
        assertTrue(matching.contains("matching2.context_cb_pc = cbFunc.address()"))

        val manager = native("modules/SceNpManager/SceNpManager.cpp")
        assertTrue(manager.contains("events.swap(matching2.pending)"))
        assertTrue(manager.contains("thread->run_callback(context_cb_pc"))
    }

    @Test fun downscaleStagesThroughGuestMemoryHelpers() {
        val mem = native("mem/include/mem/functions.h")
        assertTrue(mem.contains("void memcpy_to_guest(MemState &mem, Address dst, const void *src, uint32_t size)"))
        assertTrue(mem.contains("void memcpy_from_guest(MemState &mem, void *dst, Address src, uint32_t size)"))

        val transfer = native("renderer/src/transfer.cpp")
        assertTrue(transfer.contains("static uint64_t transfer_region_bytes"))
        assertTrue(transfer.contains("memcpy_from_guest(mem, src_staging.data()"))
        assertTrue(transfer.contains("memcpy_to_guest(mem, dst_addr, dst_staging.data()"))

        val surfaceCache = native("renderer/src/vulkan/surface_cache.cpp")
        assertTrue(surfaceCache.contains("VKSurfaceCache::find_color_surface_containing"))
    }

    @Test fun gxmClipPlanesAreTranslatedToClipDistance() {
        val features = native("features/include/features/state.h")
        assertTrue(features.contains("bool support_gxm_clip_planes = false"))

        val shader = native("shader/src/spirv_recompiler.cpp")
        assertTrue(shader.contains("if (translation_state.is_vulkan && features.support_gxm_clip_planes)"))
        assertTrue(shader.contains("features.support_gxm_clip_planes = true"))

        val renderer = native("renderer/src/vulkan/renderer.cpp")
        assertTrue(renderer.contains("features.support_gxm_clip_planes = static_cast<bool>(physical_device_features.shaderClipDistance)"))
    }

    @Test fun externalRepeatModeUsesScalarIndexForFpInternal() {
        val translator = native("shader/include/shader/usse_translator.h")
        val external = translator.substringAfter("if (repeat_mode == RepeatMode::EXTERNAL && bank != RegisterBank::FPINTERNAL)")
        assertTrue(external.substringBefore("if (repeat_mode == RepeatMode::SLMSI)").contains("return repeat_index;"))
    }
}
