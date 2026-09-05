package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class UpstreamCoreRegressionContractTest {
    private val app = sequenceOf(Path.of(System.getProperty("user.dir")), Path.of(System.getProperty("user.dir"), "app"))
        .first { Files.isDirectory(it.resolve("src/main")) }

    private fun native(path: String) = app.resolve("src/main/cpp/vita3k/vita3k/$path").readText()

    @Test fun invalidNgsPatchIsResolvedThroughOwnedVoicesBeforeDereference() {
        val source = native("modules/SceNgsUser/SceNgs.cpp")
        val remove = source.substringAfter("EXPORT(int, sceNgsPatchRemoveRouting").substringBefore("EXPORT(int, sceNgsRackGetRequiredMemorySize")
        assertTrue(source.contains("static ngs::Voice *find_patch_source_voice"))
        assertTrue(remove.contains("find_patch_source_voice(emuenv.ngs, emuenv.mem, patch)"))
        assertFalse(remove.contains("patch.get(emuenv.mem)->source"))
    }

    @Test fun displayCallbackRemainsQueuedUntilGuestCallbackIsFinished() {
        val worker = native("modules/SceGxm/SceGxm.cpp")
            .substringAfter("static void display_entry_thread")
            .substringBefore("static Ptr<void> gxmRunDeferredMemoryCallback")
        val callback = worker.indexOf("display_thread->run_guest_function")
        val normalPop = worker.indexOf("display_queue.pop()", callback)
        assertTrue(callback >= 0 && normalPop > callback)
        assertTrue(worker.substring(callback, normalPop).contains("free(emuenv.mem, display_callback->data)"))
    }

    @Test fun gxmFinishRejectsForeignContextsBeforeRendererDereference() {
        val finish = native("modules/SceGxm/SceGxm.cpp")
            .substringAfter("EXPORT(int, sceGxmFinish")
            .substringBefore("EXPORT(SceGxmPassType")
        assertTrue(finish.contains("context_addr != emuenv.gxm.immediate_context"))
        assertTrue(finish.contains("context->state.type != SCE_GXM_CONTEXT_TYPE_IMMEDIATE"))
        assertTrue(finish.contains("renderer_context != emuenv.renderer->context"))
        assertTrue(finish.indexOf("renderer_context !=") < finish.indexOf("renderer::finish"))
    }

    @Test fun selfLoaderUsesSelfSegmentInfoAndJpegAcceptsBgra() {
        val self = native("kernel/src/load_self.cpp")
        val loop = self.substringAfter("for (Elf_Half seg_index").substringBefore("const auto uncompress_segment")
        assertTrue(loop.contains("image_bytes + seg_infos[seg_index].offset"))
        assertFalse(loop.contains("self_header.header_len + seg_header.p_offset"))

        val jpeg = native("modules/SceJpegEnc/SceJpegEncUser.cpp")
        assertTrue(jpeg.contains("case SCE_JPEGENC_PIXEL_BGRA8888:"))
        assertTrue(jpeg.contains("color_space, is_bgra, inPitch"))
        assertTrue(native("codec/src/mjpeg.cpp").contains("is_bgra ? AV_PIX_FMT_BGRA : AV_PIX_FMT_RGBA"))
    }

    @Test fun timedCondvarReacquiresMutexWithoutReusingExpiredTimeout() {
        val wait = native("kernel/src/sync_primitives.cpp")
            .substringAfter("int condvar_wait(")
            .substringBefore("int condvar_signal(")
        assertTrue(wait.indexOf("handle_timeout(") < wait.indexOf("mutex_lock_impl("))
        assertTrue(wait.contains("condvar->associated_mutex, weight, nullptr, false"))
        assertTrue(wait.contains("return wait_result == SCE_KERNEL_OK ? lock_result : wait_result"))
    }

    @Test fun selectedPlusShaderFixesDoNotChangeGlobalRendererDefaults() {
        val ialu = native("shader/src/translator/ialu.cpp")
        for (function in listOf("i8mad(", "i16mad(")) {
            val body = ialu.substringAfter("USSETranslatorVisitor::$function").substringBefore("return true;")
            assertTrue(body.contains("set_repeat_multiplier(1, 1, 1, 1)"))
            assertTrue(body.contains("reset_repeat_multiplier()"))
        }

        val data = native("shader/src/translator/data.cpp")
        assertTrue(data.contains("const std::vector<spv::Id> conditions(result_components, cond_result)"))
        val renderer = native("renderer/src/vulkan/renderer.cpp")
        assertTrue(renderer.contains("features.force_full_precision = cfg.force_full_precision"))
        assertTrue(native("config/include/config/state.h").contains("std::string memory_mapping = \"double-buffer\""))
        assertTrue(native("config/include/config/config.h").contains("code(int, \"guest-cores\", 1, guest_cores)"))
        val selection = renderer.substringAfter("auto &config_mapping = cfg.current_config.memory_mapping")
            .substringBefore("features.enable_memory_mapping")
        assertFalse(selection.contains("physical_device_properties.vendorID"))
    }
}
