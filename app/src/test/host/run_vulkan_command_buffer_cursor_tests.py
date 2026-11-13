#!/usr/bin/env python3
"""Run the production frame-slot command-buffer cursor regression."""
from pathlib import Path
import os
import subprocess

root = Path(__file__).resolve().parents[4]
include = root / 'app/src/main/cpp/vita3k/vita3k/renderer/include'
types_path = include / 'renderer/vulkan/types.h'
context_path = root / 'app/src/main/cpp/vita3k/vita3k/renderer/src/vulkan/context.cpp'
state_path = include / 'renderer/vulkan/state.h'
types = types_path.read_text()
context = context_path.read_text()
state = state_path.read_text()
assert 'FrameCommandBufferCursor<MAX_FRAMES_RENDERING> command_buffer_cursor;' in types, \
    'VKRenderTarget must own a per-frame-slot command-buffer cursor'
assert 'int cmd_buffer_idx = 0;' not in types and 'uint64_t last_used_frame = 0;' not in types, \
    'VKRenderTarget must not retain the old target-wide frame cursor'
assert 'command_buffer_cursor.acquire(frame_idx, frame.pool_epoch)' in context, \
    'start_recording must select a command buffer using the shared frame-pool epoch'
assert 'uint64_t frame_epoch = 1;' in state and 'context.state.frame_epoch++;' in context, \
    'frame-slot epochs must advance globally, not with one context-local timestamp'

out = root / 'app/build/vulkan-command-buffer-cursor-probe'
out.mkdir(parents=True, exist_ok=True)
source = out / 'probe.cpp'
source.write_text(r'''#include <renderer/vulkan/frame_command_buffer_cursor.h>
#include <cstdint>
#include <cstdio>

using renderer::vulkan::FrameCommandBufferCursor;
int checks = 0;
int failures = 0;
void check(bool condition, const char *message) {
    ++checks;
    if (!condition) { ++failures; std::printf("FAIL %s\n", message); }
}
int main() {
    FrameCommandBufferCursor<3> cursor;

    // Reproduce the former scalar cursor: a second context's unrelated local
    // timestamp rewinds the same target and selects its still-pending CB again.
    uint64_t old_last_used_frame = 0;
    std::size_t old_next_index = 0;
    auto old_acquire = [&](uint64_t context_timestamp) {
        if (old_last_used_frame != context_timestamp) {
            old_next_index = 0;
            old_last_used_frame = context_timestamp;
        }
        return old_next_index++;
    };
    const auto old_a = old_acquire(82);
    const auto old_b = old_acquire(7);
    check(old_a == old_b, "legacy scalar cursor reproduces pending command-buffer reuse");

    // The production helper is keyed by the shared frame-slot epoch instead.
    const auto select_for_context = [&](uint64_t context_timestamp) {
        (void)context_timestamp; // context-local value must not choose/reset the CB cursor
        return cursor.acquire(2, 41);
    };
    check(select_for_context(82) == 0, "first context starts at the slot's first buffer");
    check(select_for_context(7) == 1, "second context advances the shared slot cursor");

    check(cursor.acquire(0, 40) == 0, "other frame slot has an independent cursor");
    check(cursor.acquire(0, 40) == 1, "other contexts share that slot cursor");

    // Returning to slot 2 happens only after its pool/fences are reset. The new
    // epoch lets that slot safely start over at buffer zero.
    check(cursor.acquire(2, 44) == 0, "pool reset epoch starts slot over at buffer zero");
    check(cursor.acquire(2, 44) == 1, "slot remains shared within the new epoch");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
''')
compiler = Path('C:/msys64/ucrt64/bin/g++.exe')
env = os.environ.copy()
env['PATH'] = str(compiler.parent) + ';' + env.get('PATH', '')
subprocess.run([str(compiler), '-std=c++20', '-O0', '-I', str(include), str(source), '-o', str(out / 'probe.exe')], check=True, env=env)
raise SystemExit(subprocess.run([str(out / 'probe.exe')], env=env).returncode)

