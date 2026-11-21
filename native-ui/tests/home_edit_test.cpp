#include <emucorev/paf/home_edit.h>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(msg);
}
} // namespace

int main() {
    using namespace emucorev::paf;
    try {
        const std::vector<std::string> catalog = {
            "PCSA0001", "PCSA0002", "PCSA0003", "PCSA0004", "PCSA0005",
            "PCSA0006", "PCSA0007", "PCSA0008", "PCSA0009", "PCSA0010",
            "PCSA0011", "PCSA0012",
        };
        HomeDesktopState desktop;
        desktop.sync_with_catalog(catalog);
        require(desktop.page_count() == 2, "12 games span 2 pages");
        require(desktop.entry_at(0, 0)->id == "PCSA0001", "Slot 0 is first game");
        require(desktop.entry_at(1, 1)->id == "PCSA0012", "Page 1 slot 1 is 12th game");

        require(desktop.move_or_swap_slot(0, 0, 1, 5), "Move to empty slot on page 1");
        require(desktop.entry_at(0, 0) == nullptr, "Source slot cleared after move");
        require(desktop.entry_at(1, 5)->id == "PCSA0001", "Moved app arrived on page 1 slot 5");

        require(desktop.drop_onto_slot(0, 1, 0, 2, true, "Games"), "Drop creates folder");
        const auto* folder = desktop.entry_at(0, 2);
        require(folder && folder->kind == HomeEntryKind::folder, "Destination became folder");
        require(folder->members.size() == 2 && folder->title == "Games", "Folder holds 2 apps");
        require(desktop.entry_at(0, 1) == nullptr, "Dropped app slot cleared");

        require(desktop.drop_onto_slot(0, 3, 0, 2, true), "Drop third app into folder");
        require(desktop.entry_at(0, 2)->members.size() == 3, "Folder grew to 3 members");

        const std::string saved = desktop.serialize_layout(catalog);
        HomeDesktopState restored;
        restored.apply_serialized_layout(saved, catalog);
        require(restored.page_count() == 2, "Restored page count matches");
        require(restored.entry_at(1, 5) && restored.entry_at(1, 5)->id == "PCSA0001",
            "Restored moved app position");
        const auto* rf = restored.entry_at(0, 2);
        require(rf && rf->kind == HomeEntryKind::folder && rf->members.size() == 3
            && rf->title == "Games", "Restored folder contents");

        require(restored.remove_from_folder(0, 2, 1), "Extract member from folder");
        require(restored.entry_at(0, 2)->members.size() == 2, "Folder now has 2 members");

        require(restored.ungroup_folder(0, 2), "Ungroup folder");
        require(restored.entry_at(0, 2)->kind == HomeEntryKind::app, "Folder slot replaced by app");

        const auto next_right = restored.dpad_neighbor(0, 0, HomeDpadDirection::right);
        require(restored.entry_at(next_right.first, next_right.second) != nullptr,
            "Sparse DPAD lands on occupied slot");

        HomeEditVisualState vis;
        vis.set_edit_mode(true);
        for (int i = 0; i < 20; ++i) vis.step(1.0f / 60.0f);
        require(vis.edit_blend > 0.99f, "Edit mode blends to 1.0");
        const float r0 = vis.edit_wiggle_roll_rad(0, 0);
        vis.step(0.1f);
        const float r1 = vis.edit_wiggle_roll_rad(0, 0);
        require(std::abs(r1 - r0) > 0.001f, "Edit wiggle oscillates over time");

        vis.open_folder(0, 2, 251.0f, 130.0f);
        for (int i = 0; i < 20; ++i) vis.step(1.0f / 60.0f);
        require(vis.folder_blend > 0.99f && vis.is_folder_visible(), "Folder overlay opens smoothly");
        vis.close_folder();
        for (int i = 0; i < 20; ++i) vis.step(1.0f / 60.0f);
        require(!vis.is_folder_visible() && vis.folder_blend == 0.0f, "Folder overlay closes cleanly");

        std::cout << "PASS home_edit_test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
