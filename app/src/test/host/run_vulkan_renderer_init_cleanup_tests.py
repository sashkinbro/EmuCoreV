#!/usr/bin/env python3
"""Fault-inject the production renderer init and partial ScreenRenderer cleanup bodies."""
from pathlib import Path
import os
import shutil
import subprocess

root = Path(__file__).resolve().parents[4]


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise ValueError(f"unterminated function: {signature}")


creation = (root / "app/src/main/cpp/vita3k/vita3k/renderer/src/creation.cpp").read_text(encoding="utf-8")
screen = (root / "app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/screen_renderer.cpp").read_text(encoding="utf-8")
init_body = function_body(creation, "bool init(FrameHost &frame, std::unique_ptr<State> &state")
cleanup_body = function_body(screen, "void ScreenRenderer::cleanup()")

cpp = r'''
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

int log_count = 0;
#define LOG_ERROR(...) (++log_count)

namespace renderer {
enum class Backend { OpenGL, Vulkan, Unsupported };
struct FrameHost {};
struct Root {};
struct Config { struct { int gpu_idx = 7; } current_config; };
struct Queue { int maxPendingCount_ = 0; };
struct State {
    virtual ~State() = default;
    FrameHost* frame = nullptr;
    Backend current_backend = Backend::Unsupported;
    Queue command_buffer_queue;
    void init_paths(const Root&) { if (throw_paths) throw std::runtime_error("paths failed"); }
    static bool throw_paths;
};
bool State::throw_paths = false;
namespace gl {
struct GLState : State {};
enum class Outcome { Succeed, ReturnFalse, Throw };
Outcome outcome = Outcome::Succeed;
bool create(std::unique_ptr<State>&, const Config&) {
    if (outcome == Outcome::Throw) throw std::runtime_error("GL init failed");
    return outcome == Outcome::Succeed;
}
}
namespace vulkan {
struct VKState : State { explicit VKState(int value) : gpu(value) {} int gpu; };
gl::Outcome outcome = gl::Outcome::Succeed;
bool create(std::unique_ptr<State>&, const Config&) {
    if (outcome == gl::Outcome::Throw) throw std::runtime_error("Vulkan init failed");
    return outcome == gl::Outcome::Succeed;
}
}
bool init(FrameHost &frame, std::unique_ptr<State> &state, Backend backend, const Config &config, const Root &root_paths) {
''' + init_body + r'''
}
}

namespace vk {
struct Handle {
    int id = 0;
    Handle() = default;
    Handle(std::nullptr_t) {}
    Handle(int value) : id(value) {}
    explicit operator bool() const { return id != 0; }
};
using Fence = Handle;
using Semaphore = Handle;
using Framebuffer = Handle;
using ImageView = Handle;
using SwapchainKHR = Handle;
using RenderPass = Handle;
using Buffer = Handle;
using SurfaceKHR = Handle;
using CommandBuffer = Handle;
}
namespace vma { using Allocation = int; struct AllocationInfo {}; }
namespace vkutil {
struct Image { int* destroyed = nullptr; void destroy() { if (destroyed) ++*destroyed; } };
}
struct Device {
    int waits = 0;
    std::vector<int> destroyed;
    void waitIdle() { ++waits; }
    void destroy(vk::Handle handle) { if (handle) destroyed.push_back(handle.id); }
};
struct Instance {
    std::vector<int> destroyed;
    void destroy(vk::SurfaceKHR surface) { if (surface) destroyed.push_back(surface.id); }
};
struct Allocator {
    std::vector<int> destroyed;
    void destroyBuffer(vk::Buffer buffer, vma::Allocation) { if (buffer) destroyed.push_back(buffer.id); }
};
struct VulkanState { Device device; Instance instance; Allocator allocator; };
struct Filter {};
struct ScreenRenderer {
    VulkanState& state;
    vk::SurfaceKHR surface;
    vk::SwapchainKHR swapchain;
    uint32_t swapchain_size = 0;
    std::unique_ptr<Filter> filter;
    std::vector<vkutil::Image> vita_surface;
    vk::Buffer vita_surface_staging;
    vma::Allocation vita_surface_staging_alloc = 0;
    vma::AllocationInfo vita_surface_staging_info;
    std::vector<vk::Framebuffer> swapchain_framebuffers;
    vk::RenderPass default_render_pass;
    vk::RenderPass post_filter_render_pass;
    vk::RenderPass stock_adreno_pass;
    std::vector<vk::ImageView> swapchain_views;
    std::vector<vk::Fence> fences;
    std::vector<vk::Semaphore> image_acquired_semaphores;
    std::vector<vk::Semaphore> image_ready_semaphores;
    std::vector<vk::CommandBuffer> command_buffers;
    void cleanup() {
''' + cleanup_body + r'''
    }
};

int main() {
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
        failures += !ok;
    };

    renderer::FrameHost frame;
    renderer::Config config;
    renderer::Root root;
    std::unique_ptr<renderer::State> state;
    renderer::State::throw_paths = false;
    renderer::gl::outcome = renderer::gl::Outcome::Throw;
    const int logs_before_gl_throw = log_count;
    const bool gl_result = renderer::init(frame, state, renderer::Backend::OpenGL, config, root);
    check(!gl_result && state != nullptr, "GL exception returns false and preserves partial state for caller cleanup");
    check(log_count == logs_before_gl_throw + 1, "GL exception is logged at the renderer init boundary");

    state.reset();
    renderer::vulkan::outcome = renderer::gl::Outcome::Throw;
    const int logs_before_vk_throw = log_count;
    const bool vk_result = renderer::init(frame, state, renderer::Backend::Vulkan, config, root);
    check(!vk_result && state != nullptr, "Vulkan exception returns false and preserves partial state for caller cleanup");
    check(log_count == logs_before_vk_throw + 1, "Vulkan exception is logged at the renderer init boundary");
    check(static_cast<renderer::vulkan::VKState*>(state.get())->gpu == config.current_config.gpu_idx,
        "partially created Vulkan state retains its constructor configuration");

    state.reset();
    renderer::State::throw_paths = true;
    renderer::gl::outcome = renderer::gl::Outcome::Succeed;
    const bool path_result = renderer::init(frame, state, renderer::Backend::OpenGL, config, root);
    check(!path_result && state != nullptr, "path initialization exception is also contained without discarding state");
    renderer::State::throw_paths = false;

    state.reset();
    renderer::gl::outcome = renderer::gl::Outcome::ReturnFalse;
    check(!renderer::init(frame, state, renderer::Backend::OpenGL, config, root) && state != nullptr,
        "ordinary backend failure keeps the allocated renderer for caller cleanup");
    state.reset();

    renderer::gl::outcome = renderer::gl::Outcome::Succeed;
    check(renderer::init(frame, state, renderer::Backend::OpenGL, config, root), "successful init still completes");
    check(state->current_backend == renderer::Backend::OpenGL && state->command_buffer_queue.maxPendingCount_ == 30,
        "successful init still finalizes backend and queue configuration");
    state.reset();

    VulkanState vk_state;
    ScreenRenderer empty(vk_state);
    empty.swapchain_size = 4; // setup may have failed before any sync vectors were allocated
    empty.cleanup();
    check(vk_state.device.destroyed.empty(), "cleanup with no created sync objects never indexes by swapchain size");
    check(empty.fences.empty() && empty.image_acquired_semaphores.empty() && empty.image_ready_semaphores.empty(),
        "empty partial setup remains clean after cleanup");

    ScreenRenderer partial(vk_state);
    partial.swapchain_size = 5; // setup failed before create_layout_sync completed
    partial.fences = {{11}, {0}};
    partial.image_acquired_semaphores = {{21}, {0}, {22}};
    partial.image_ready_semaphores = {{31}};
    partial.cleanup();
    check(vk_state.device.destroyed == std::vector<int>({11, 21, 22, 31}),
        "cleanup destroys only vector-owned non-null partial sync handles");
    check(partial.fences.empty() && partial.image_acquired_semaphores.empty() && partial.image_ready_semaphores.empty(),
        "cleanup clears each independently sized sync vector");
    const auto destroyed_once = vk_state.device.destroyed;
    partial.cleanup();
    check(vk_state.device.destroyed == destroyed_once, "repeated partial cleanup does not destroy sync handles twice");
    check(failures == 0, "all fault-injection cases pass");
    return failures ? 1 : 0;
}
'''

out = root / "app/build/vulkan-renderer-init-cleanup-probe"
out.mkdir(parents=True, exist_ok=True)
source = out / "probe.cpp"
binary = out / "probe.exe"
source.write_text(cpp, encoding="utf-8")
if os.name == "nt":
    os.environ["PATH"] = "C:/msys64/ucrt64/bin;" + os.environ["PATH"]
compiler = os.environ.get("CXX") or shutil.which("g++") or "C:/msys64/ucrt64/bin/g++.exe"
subprocess.run([compiler, "-std=c++20", str(source), "-o", str(binary)], check=True)
result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
print(result.stdout + result.stderr, end="")
(out / ("red.log" if result.returncode else "green.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
raise SystemExit(result.returncode)
