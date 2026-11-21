#pragma once

#include <emucorev/paf/home_layout.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace emucorev::paf {

inline constexpr int kHomeMaxPages = 10;
inline constexpr int kHomeMaxFolderItems = 10;

enum class HomeEntryKind {
    app,
    folder,
};

struct HomeSlotEntry {
    HomeEntryKind kind = HomeEntryKind::app;
    std::string id;
    std::string title;
    int catalog_index = -1;       // valid when kind == app
    std::vector<int> members;     // catalog indices when kind == folder (max 10)
};

struct HomeDesktopPage {
    std::array<std::optional<HomeSlotEntry>, kHomeSlotsPerPage> slots{};
};

// Mutable PS Vita Home desktop model supporting multi-page arrangement,
// drag reorder/swap, folder creation/opening/ungrouping, and layout persistence.
class HomeDesktopState {
public:
    HomeDesktopState();

    void sync_with_catalog(const std::vector<std::string>& catalog_ids);
    void apply_serialized_layout(std::string_view text, const std::vector<std::string>& catalog_ids);
    [[nodiscard]] std::string serialize_layout(const std::vector<std::string>& catalog_ids) const;

    [[nodiscard]] int page_count() const noexcept { return static_cast<int>(pages_.size()); }
    [[nodiscard]] const HomeDesktopPage& page(int index) const;
    [[nodiscard]] const HomeSlotEntry* entry_at(int page, int slot) const noexcept;
    [[nodiscard]] HomeSlotEntry* entry_at(int page, int slot) noexcept;

    int add_page();
    void prune_trailing_empty_pages();

    bool move_or_swap_slot(int src_page, int src_slot, int dst_page, int dst_slot);
    // Drops (src_page, src_slot) onto (dst_page, dst_slot).
    // - Empty dst -> move
    // - Folder dst + App src -> append into folder if < 10 members
    // - App dst + App src + allow_create_folder -> create new folder containing [dst, src]
    // - Otherwise swap
    bool drop_onto_slot(int src_page, int src_slot, int dst_page, int dst_slot,
        bool allow_create_folder, std::string_view default_folder_title = "New Folder");

    bool create_folder_at(int page, int slot, std::string_view default_folder_title = "New Folder");
    bool remove_from_folder(int folder_page, int folder_slot, int member_index);
    bool ungroup_folder(int folder_page, int folder_slot);

    [[nodiscard]] std::pair<int, int> find_first_occupied(int preferred_page = 0) const noexcept;
    [[nodiscard]] std::pair<int, int> dpad_neighbor(int page, int slot,
        HomeDpadDirection direction) const noexcept;

private:
    std::pair<int, int> allocate_free_slot(int preferred_page);
    std::string next_folder_id();
    bool page_is_empty(int page) const noexcept;

    std::vector<HomeDesktopPage> pages_;
    int next_folder_seq_ = 1;
};

// Smooth animation state for Home Edit Mode wiggle (shell.elf FUN_810cc946)
// and translucent Folder overlay open/close transitions.
struct HomeEditVisualState {
    bool edit_mode = false;
    float edit_blend = 0.0f;
    float wiggle_time = 0.0f;

    bool folder_open = false;
    int open_folder_page = -1;
    int open_folder_slot = -1;
    float folder_blend = 0.0f;
    float folder_origin_x = kHomeScrollCenterX;
    float folder_origin_y = kHomeScrollCenterY;

    void set_edit_mode(bool enabled) noexcept { edit_mode = enabled; }
    void open_folder(int page, int slot, float origin_x, float origin_y) noexcept;
    void close_folder() noexcept { folder_open = false; }

    void step(float dt_seconds) noexcept;

    [[nodiscard]] float edit_wiggle_roll_rad(int page, int slot) const noexcept;
    [[nodiscard]] float edit_wiggle_lift_y(int page, int slot) const noexcept;
    [[nodiscard]] bool is_folder_visible() const noexcept { return folder_open || folder_blend > 0.005f; }
};

} // namespace emucorev::paf
