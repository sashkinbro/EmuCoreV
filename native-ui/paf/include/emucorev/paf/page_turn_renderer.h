#pragma once
#include <emucorev/paf/page_turn.h>
#include <array>
#include <memory>
#include <span>
#include <string_view>

namespace emucorev::paf {
struct PageSurfaceShaders { std::string_view vertex, fragment; };
struct PageQuadVertex { PagePoint position, uv; };
enum class PageSurface { front, folded, empty_back };
struct PageSurfaceDraw {
    // Reviewed parameter layouts, supplied by the native material controller.
    std::span<const float> vertex_parameters, fragment_parameters;
    std::array<float, 12> viewport, fragment_state;
    // Borrowed GLES texture names: fragment units 0..3, vertex lookup unit 4.
    std::array<unsigned, 5> textures;
    bool blend;
    // Native-preview composition extension. PAF meta-alpha is not part of the
    // recovered GX fragment block; its reserved tail float carries opacity.
    float meta_alpha = 1.f;
};

// Created/destroyed on the owning GLES context thread. No firmware, guest state,
// shader translation, file access or allocations during mesh updates and draws.
// The host supplies the material shaders; this class does not approximate them.
class PageTurnRenderer {
public:
    PageTurnRenderer(PageSurfaceShaders front, PageSurfaceShaders folded,
        PageSurfaceShaders rectangle = {}, PageSurfaceShaders empty_back = {});
    ~PageTurnRenderer();
    PageTurnRenderer(const PageTurnRenderer&) = delete;
    PageTurnRenderer& operator=(const PageTurnRenderer&) = delete;
    void upload_mesh(const PageTurnMesh& mesh);
    // Caller owns framebuffer, viewport and scissor. Both winding directions
    // are retained: the fold material distinguishes front/back faces itself.
    void draw(PageSurface surface, const PageSurfaceDraw& material);
    // Four position/UV pairs in triangle-strip order. The optional rectangle
    // shaders must be provided at construction; no lazy compilation occurs.
    void draw_quad(std::span<const PageQuadVertex, 4> vertices, const PageSurfaceDraw& material);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
