#include <emucorev/paf/home_edit.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace emucorev::paf {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kEditTransitionSeconds = 0.20f;
constexpr float kFolderTransitionSeconds = 0.24f;

int slot_row(int slot) noexcept {
    if (slot < 3) return 0;
    if (slot < 7) return 1;
    return 2;
}

std::pair<int, int> row_bounds(int row) noexcept {
    if (row == 0) return {0, 3};
    if (row == 1) return {3, 7};
    return {7, 10};
}

std::uint32_t hash_pair(int a, int b) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(a * 73 + b * 29 + 0x9e3779b9u);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    return h;
}

std::string sanitize_token(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        if (c == '\r' || c == '\n' || c == '|' || c == '\t') out.push_back(' ');
        else out.push_back(c);
    }
    return out;
}

} // namespace

HomeDesktopState::HomeDesktopState() {
    pages_.resize(1);
}

const HomeDesktopPage& HomeDesktopState::page(int index) const {
    return pages_.at(static_cast<std::size_t>(std::clamp(index, 0, page_count() - 1)));
}

const HomeSlotEntry* HomeDesktopState::entry_at(int page_index, int slot) const noexcept {
    if (page_index < 0 || page_index >= page_count() || slot < 0 || slot >= kHomeSlotsPerPage)
        return nullptr;
    const auto& cell = pages_[static_cast<std::size_t>(page_index)].slots[static_cast<std::size_t>(slot)];
    return cell ? &(*cell) : nullptr;
}

HomeSlotEntry* HomeDesktopState::entry_at(int page_index, int slot) noexcept {
    if (page_index < 0 || page_index >= page_count() || slot < 0 || slot >= kHomeSlotsPerPage)
        return nullptr;
    auto& cell = pages_[static_cast<std::size_t>(page_index)].slots[static_cast<std::size_t>(slot)];
    return cell ? &(*cell) : nullptr;
}

bool HomeDesktopState::page_is_empty(int page_index) const noexcept {
    if (page_index < 0 || page_index >= page_count()) return true;
    for (const auto& cell : pages_[static_cast<std::size_t>(page_index)].slots) {
        if (cell.has_value()) return false;
    }
    return true;
}

int HomeDesktopState::add_page() {
    if (page_count() >= kHomeMaxPages) return page_count() - 1;
    pages_.emplace_back();
    return page_count() - 1;
}

void HomeDesktopState::prune_trailing_empty_pages() {
    while (pages_.size() > 1 && page_is_empty(page_count() - 1)) {
        pages_.pop_back();
    }
}

std::string HomeDesktopState::next_folder_id() {
    return "folder_" + std::to_string(next_folder_seq_++);
}

std::pair<int, int> HomeDesktopState::allocate_free_slot(int preferred_page) {
    const int start_page = std::clamp(preferred_page, 0, page_count() - 1);
    for (int p = start_page; p < page_count(); ++p) {
        for (int s = 0; s < kHomeSlotsPerPage; ++s) {
            if (!pages_[static_cast<std::size_t>(p)].slots[static_cast<std::size_t>(s)].has_value()) {
                return {p, s};
            }
        }
    }
    for (int p = 0; p < start_page; ++p) {
        for (int s = 0; s < kHomeSlotsPerPage; ++s) {
            if (!pages_[static_cast<std::size_t>(p)].slots[static_cast<std::size_t>(s)].has_value()) {
                return {p, s};
            }
        }
    }
    if (page_count() < kHomeMaxPages) {
        const int np = add_page();
        return {np, 0};
    }
    return {-1, -1};
}

void HomeDesktopState::sync_with_catalog(const std::vector<std::string>& catalog_ids) {
    std::unordered_map<std::string, int> id_to_index;
    id_to_index.reserve(catalog_ids.size());
    for (std::size_t i = 0; i < catalog_ids.size(); ++i) {
        id_to_index[catalog_ids[i]] = static_cast<int>(i);
    }

    std::unordered_set<int> placed;
    if (pages_.empty()) pages_.resize(1);

    for (auto& desktop_page : pages_) {
        for (auto& cell : desktop_page.slots) {
            if (!cell) continue;
            if (cell->kind == HomeEntryKind::app) {
                const auto it = id_to_index.find(cell->id);
                if (it == id_to_index.end() || placed.contains(it->second)) {
                    cell.reset();
                } else {
                    cell->catalog_index = it->second;
                    placed.insert(it->second);
                }
            } else {
                std::vector<int> valid_members;
                for (const int idx : cell->members) {
                    if (idx >= 0 && static_cast<std::size_t>(idx) < catalog_ids.size()
                        && !placed.contains(idx)
                        && static_cast<int>(valid_members.size()) < kHomeMaxFolderItems) {
                        valid_members.push_back(idx);
                        placed.insert(idx);
                    }
                }
                if (valid_members.empty()) {
                    cell.reset();
                } else {
                    cell->members = std::move(valid_members);
                }
            }
        }
    }

    for (std::size_t i = 0; i < catalog_ids.size(); ++i) {
        const int idx = static_cast<int>(i);
        if (placed.contains(idx)) continue;
        const auto [p, s] = allocate_free_slot(0);
        if (p < 0) break;
        HomeSlotEntry entry{};
        entry.kind = HomeEntryKind::app;
        entry.id = catalog_ids[i];
        entry.catalog_index = idx;
        pages_[static_cast<std::size_t>(p)].slots[static_cast<std::size_t>(s)] = std::move(entry);
        placed.insert(idx);
    }

    prune_trailing_empty_pages();
}

void HomeDesktopState::apply_serialized_layout(std::string_view text,
    const std::vector<std::string>& catalog_ids) {
    pages_.clear();
    pages_.resize(1);
    next_folder_seq_ = 1;

    std::unordered_map<std::string, int> id_to_index;
    id_to_index.reserve(catalog_ids.size());
    for (std::size_t i = 0; i < catalog_ids.size(); ++i) {
        id_to_index[catalog_ids[i]] = static_cast<int>(i);
    }

    std::istringstream in{std::string(text)};
    std::string line;
    std::unordered_set<int> used;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] != 'P') continue;
        std::istringstream ls(line);
        char tag = 0;
        int p = -1, s = -1;
        char kind = 0;
        if (!(ls >> tag >> p >> s >> kind)) continue;
        if (p < 0 || p >= kHomeMaxPages || s < 0 || s >= kHomeSlotsPerPage) continue;
        while (page_count() <= p) pages_.emplace_back();
        auto& cell = pages_[static_cast<std::size_t>(p)].slots[static_cast<std::size_t>(s)];
        if (cell.has_value()) continue;

        if (kind == 'A') {
            std::string app_id;
            if (!(ls >> app_id)) continue;
            const auto it = id_to_index.find(app_id);
            if (it == id_to_index.end() || used.contains(it->second)) continue;
            HomeSlotEntry entry{};
            entry.kind = HomeEntryKind::app;
            entry.id = app_id;
            entry.catalog_index = it->second;
            cell = std::move(entry);
            used.insert(it->second);
        } else if (kind == 'F') {
            std::string folder_id;
            if (!(ls >> folder_id)) continue;
            std::string rest;
            std::getline(ls, rest);
            const auto bar = rest.find('|');
            if (bar == std::string::npos) continue;
            std::string title = rest.substr(0, bar);
            while (!title.empty() && title.front() == ' ') title.erase(title.begin());
            while (!title.empty() && title.back() == ' ') title.pop_back();
            std::string members_csv = rest.substr(bar + 1);
            std::vector<int> members;
            std::istringstream ms(members_csv);
            std::string tok;
            while (std::getline(ms, tok, ',')) {
                while (!tok.empty() && tok.front() == ' ') tok.erase(tok.begin());
                while (!tok.empty() && tok.back() == ' ') tok.pop_back();
                const auto it = id_to_index.find(tok);
                if (it != id_to_index.end() && !used.contains(it->second)
                    && static_cast<int>(members.size()) < kHomeMaxFolderItems) {
                    members.push_back(it->second);
                    used.insert(it->second);
                }
            }
            if (members.empty()) continue;
            HomeSlotEntry entry{};
            entry.kind = HomeEntryKind::folder;
            entry.id = folder_id;
            entry.title = title.empty() ? "Folder" : title;
            entry.members = std::move(members);
            cell = std::move(entry);
            if (folder_id.rfind("folder_", 0) == 0) {
                try {
                    const int seq = std::stoi(folder_id.substr(7));
                    next_folder_seq_ = std::max(next_folder_seq_, seq + 1);
                } catch (...) {}
            }
        }
    }

    sync_with_catalog(catalog_ids);
}

std::string HomeDesktopState::serialize_layout(const std::vector<std::string>& catalog_ids) const {
    std::ostringstream out;
    out << "V1\n";
    for (int p = 0; p < page_count(); ++p) {
        for (int s = 0; s < kHomeSlotsPerPage; ++s) {
            const auto* e = entry_at(p, s);
            if (!e) continue;
            if (e->kind == HomeEntryKind::app) {
                out << "P " << p << ' ' << s << " A " << sanitize_token(e->id) << '\n';
            } else {
                out << "P " << p << ' ' << s << " F " << sanitize_token(e->id) << ' '
                    << sanitize_token(e->title) << " |";
                bool first = true;
                for (const int idx : e->members) {
                    if (idx < 0 || static_cast<std::size_t>(idx) >= catalog_ids.size()) continue;
                    out << (first ? ' ' : ',') << sanitize_token(catalog_ids[static_cast<std::size_t>(idx)]);
                    first = false;
                }
                out << '\n';
            }
        }
    }
    return out.str();
}

bool HomeDesktopState::move_or_swap_slot(int src_page, int src_slot, int dst_page, int dst_slot) {
    if (src_page < 0 || src_page >= page_count() || src_slot < 0 || src_slot >= kHomeSlotsPerPage)
        return false;
    if (dst_page < 0 || dst_page >= page_count() || dst_slot < 0 || dst_slot >= kHomeSlotsPerPage)
        return false;
    if (src_page == dst_page && src_slot == dst_slot) return false;
    auto& a = pages_[static_cast<std::size_t>(src_page)].slots[static_cast<std::size_t>(src_slot)];
    auto& b = pages_[static_cast<std::size_t>(dst_page)].slots[static_cast<std::size_t>(dst_slot)];
    if (!a.has_value()) return false;
    std::swap(a, b);
    return true;
}

bool HomeDesktopState::drop_onto_slot(int src_page, int src_slot, int dst_page, int dst_slot,
    bool allow_create_folder, std::string_view default_folder_title) {
    if (src_page < 0 || src_page >= page_count() || src_slot < 0 || src_slot >= kHomeSlotsPerPage)
        return false;
    if (dst_page < 0 || dst_page >= page_count() || dst_slot < 0 || dst_slot >= kHomeSlotsPerPage)
        return false;
    if (src_page == dst_page && src_slot == dst_slot) return false;

    auto& src = pages_[static_cast<std::size_t>(src_page)].slots[static_cast<std::size_t>(src_slot)];
    auto& dst = pages_[static_cast<std::size_t>(dst_page)].slots[static_cast<std::size_t>(dst_slot)];
    if (!src.has_value()) return false;

    if (!dst.has_value()) {
        dst = std::move(src);
        src.reset();
        return true;
    }

    if (src->kind == HomeEntryKind::app && dst->kind == HomeEntryKind::folder
        && static_cast<int>(dst->members.size()) < kHomeMaxFolderItems) {
        dst->members.push_back(src->catalog_index);
        src.reset();
        return true;
    }

    if (allow_create_folder && src->kind == HomeEntryKind::app && dst->kind == HomeEntryKind::app) {
        HomeSlotEntry folder{};
        folder.kind = HomeEntryKind::folder;
        folder.id = next_folder_id();
        folder.title = std::string(default_folder_title);
        folder.members = {dst->catalog_index, src->catalog_index};
        dst = std::move(folder);
        src.reset();
        return true;
    }

    std::swap(src, dst);
    return true;
}

bool HomeDesktopState::create_folder_at(int page_index, int slot,
    std::string_view default_folder_title) {
    auto* e = entry_at(page_index, slot);
    if (!e || e->kind != HomeEntryKind::app) return false;
    HomeSlotEntry folder{};
    folder.kind = HomeEntryKind::folder;
    folder.id = next_folder_id();
    folder.title = std::string(default_folder_title);
    folder.members = {e->catalog_index};
    pages_[static_cast<std::size_t>(page_index)].slots[static_cast<std::size_t>(slot)] = std::move(folder);
    return true;
}

bool HomeDesktopState::remove_from_folder(int folder_page, int folder_slot, int member_index) {
    auto* f = entry_at(folder_page, folder_slot);
    if (!f || f->kind != HomeEntryKind::folder) return false;
    if (member_index < 0 || static_cast<std::size_t>(member_index) >= f->members.size()) return false;

    const int cat_idx = f->members[static_cast<std::size_t>(member_index)];
    const auto [free_p, free_s] = allocate_free_slot(folder_page);
    if (free_p < 0) return false;

    f = entry_at(folder_page, folder_slot);
    f->members.erase(f->members.begin() + member_index);

    HomeSlotEntry extracted{};
    extracted.kind = HomeEntryKind::app;
    extracted.catalog_index = cat_idx;
    pages_[static_cast<std::size_t>(free_p)].slots[static_cast<std::size_t>(free_s)] = std::move(extracted);

    if (f->members.empty()) {
        pages_[static_cast<std::size_t>(folder_page)].slots[static_cast<std::size_t>(folder_slot)].reset();
    }
    return true;
}

bool HomeDesktopState::ungroup_folder(int folder_page, int folder_slot) {
    auto* f = entry_at(folder_page, folder_slot);
    if (!f || f->kind != HomeEntryKind::folder) return false;
    std::vector<int> members = std::move(f->members);
    pages_[static_cast<std::size_t>(folder_page)].slots[static_cast<std::size_t>(folder_slot)].reset();
    for (std::size_t i = 0; i < members.size(); ++i) {
        HomeSlotEntry app{};
        app.kind = HomeEntryKind::app;
        app.catalog_index = members[i];
        if (i == 0) {
            pages_[static_cast<std::size_t>(folder_page)].slots[static_cast<std::size_t>(folder_slot)] = std::move(app);
        } else {
            const auto [p, s] = allocate_free_slot(folder_page);
            if (p >= 0) {
                pages_[static_cast<std::size_t>(p)].slots[static_cast<std::size_t>(s)] = std::move(app);
            }
        }
    }
    return true;
}

std::pair<int, int> HomeDesktopState::find_first_occupied(int preferred_page) const noexcept {
    const int p0 = std::clamp(preferred_page, 0, page_count() - 1);
    for (int s = 0; s < kHomeSlotsPerPage; ++s) {
        if (entry_at(p0, s)) return {p0, s};
    }
    for (int p = 0; p < page_count(); ++p) {
        for (int s = 0; s < kHomeSlotsPerPage; ++s) {
            if (entry_at(p, s)) return {p, s};
        }
    }
    return {0, 0};
}

std::pair<int, int> HomeDesktopState::dpad_neighbor(int page_index, int slot,
    HomeDpadDirection direction) const noexcept {
    const int cp = std::clamp(page_index, 0, page_count() - 1);
    const int cs = std::clamp(slot, 0, kHomeSlotsPerPage - 1);
    if (!entry_at(cp, cs)) return find_first_occupied(cp);

    const auto& offsets = home_slot_offsets(HomeGridLayout::home);
    const float cur_dx = offsets[static_cast<std::size_t>(cs)].dx;
    const int row = slot_row(cs);

    auto pick_in_row = [&](int p, int r) -> int {
        const auto [b, e] = row_bounds(r);
        int best = -1;
        float best_d = std::numeric_limits<float>::max();
        for (int s = b; s < e; ++s) {
            if (!entry_at(p, s)) continue;
            const float d = std::abs(offsets[static_cast<std::size_t>(s)].dx - cur_dx);
            if (d < best_d) {
                best_d = d;
                best = s;
            }
        }
        return best;
    };

    if (direction == HomeDpadDirection::left || direction == HomeDpadDirection::right) {
        const auto [b, e] = row_bounds(row);
        const int step = (direction == HomeDpadDirection::left) ? -1 : 1;
        for (int s = cs + step; s >= b && s < e; s += step) {
            if (entry_at(cp, s)) return {cp, s};
        }
        return {cp, cs};
    }

    if (direction == HomeDpadDirection::down) {
        for (int r = row + 1; r <= 2; ++r) {
            const int c = pick_in_row(cp, r);
            if (c >= 0) return {cp, c};
        }
        for (int np = cp + 1; np < page_count(); ++np) {
            for (int r = 0; r <= 2; ++r) {
                const int c = pick_in_row(np, r);
                if (c >= 0) return {np, c};
            }
        }
        return {cp, cs};
    }

    for (int r = row - 1; r >= 0; --r) {
        const int c = pick_in_row(cp, r);
        if (c >= 0) return {cp, c};
    }
    for (int np = cp - 1; np >= 0; --np) {
        for (int r = 2; r >= 0; --r) {
            const int c = pick_in_row(np, r);
            if (c >= 0) return {np, c};
        }
    }
    return {cp, cs};
}

void HomeEditVisualState::open_folder(int page, int slot, float origin_x, float origin_y) noexcept {
    folder_open = true;
    open_folder_page = page;
    open_folder_slot = slot;
    folder_origin_x = origin_x;
    folder_origin_y = origin_y;
}

void HomeEditVisualState::step(float dt_seconds) noexcept {
    if (dt_seconds <= 0.0f || !std::isfinite(dt_seconds)) return;
    const float dt = std::min(dt_seconds, 0.1f);
    const float edit_target = edit_mode ? 1.0f : 0.0f;
    const float edit_delta = dt / kEditTransitionSeconds;
    if (edit_blend < edit_target) edit_blend = std::min(1.0f, edit_blend + edit_delta);
    else if (edit_blend > edit_target) edit_blend = std::max(0.0f, edit_blend - edit_delta);

    if (edit_blend > 0.0f) {
        wiggle_time += dt;
        if (wiggle_time > 1000.0f) wiggle_time -= 1000.0f;
    }

    const float folder_target = folder_open ? 1.0f : 0.0f;
    const float folder_delta = dt / kFolderTransitionSeconds;
    if (folder_blend < folder_target) folder_blend = std::min(1.0f, folder_blend + folder_delta);
    else if (folder_blend > folder_target) folder_blend = std::max(0.0f, folder_blend - folder_delta);
    if (!folder_open && folder_blend <= 0.001f) {
        folder_blend = 0.0f;
        open_folder_page = -1;
        open_folder_slot = -1;
    }
}

float HomeEditVisualState::edit_wiggle_roll_rad(int page, int slot) const noexcept {
    if (edit_blend <= 0.0f) return 0.0f;
    const float phase = static_cast<float>(hash_pair(page, slot) & 0xFFFFu) / 65535.0f * kTwoPi;
    const float freq = 2.35f + 0.25f * static_cast<float>((slot * 3) % 5) / 4.0f;
    return std::sin(wiggle_time * freq * kTwoPi + phase) * 0.056f * edit_blend;
}

float HomeEditVisualState::edit_wiggle_lift_y(int page, int slot) const noexcept {
    if (edit_blend <= 0.0f) return 0.0f;
    const float phase = static_cast<float>(hash_pair(page + 13, slot + 5) & 0xFFFFu) / 65535.0f * kTwoPi;
    return (-1.5f + std::sin(wiggle_time * 3.4f + phase) * 1.8f) * edit_blend;
}

} // namespace emucorev::paf
