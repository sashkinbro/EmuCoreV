// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// Pure derivation of the secondary battery readings (capacity, voltage, temperature, health) from
// the host charge percentage, plus the callback registry used by scePowerRegisterCallback.
// Units follow vita-headers psp2/power.h: mAh, mV, degrees Celsius * 100, percent, minutes.
// The absolute values are generic and provisional (a typical 2210 mAh Li-ion pack), not recovered
// from firmware.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace power_battery {

constexpr int FULL_CAPACITY_MAH = 2210; // provisional
constexpr int VOLT_EMPTY_MV = 3300; // provisional
constexpr int VOLT_FULL_MV = 4200; // provisional
constexpr int BATTERY_TEMP_CENTI_C = 2500; // provisional: 25.00 C
constexpr int CASE_TEMP_CENTI_C = 3000; // provisional: 30.00 C
constexpr int HEALTH_PERCENT = 100;
constexpr int CYCLE_COUNT = 0;

// SDL reports -1 when the charge is unknown (desktop without battery): treat it as full.
constexpr int normalize_percent(int percent) { return percent < 0 ? 100 : (percent > 100 ? 100 : percent); }

constexpr int remain_capacity_mah(int percent) { return FULL_CAPACITY_MAH * normalize_percent(percent) / 100; }

constexpr int voltage_mv(int percent) { return VOLT_EMPTY_MV + (VOLT_FULL_MV - VOLT_EMPTY_MV) * normalize_percent(percent) / 100; }

// Charging status: 1 while charging, 0 otherwise (provisional encoding).
constexpr int charging_status(bool charging) { return charging ? 1 : 0; }

// Remaining time in minutes from SDL's seconds (-1 = unknown -> INT_MAX like the existing query).
constexpr int life_time_minutes(int seconds) { return seconds < 0 ? 0x7FFFFFFF : seconds / 60; }

// Set of registered power callback ids (registration only; callbacks are never fired).
class CallbackRegistry {
public:
    enum Result { Ok, AlreadyRegistered, NotRegistered };

    Result add(std::int32_t id) {
        if (std::find(ids_.begin(), ids_.end(), id) != ids_.end())
            return AlreadyRegistered;
        ids_.push_back(id);
        return Ok;
    }
    Result remove(std::int32_t id) {
        const auto it = std::find(ids_.begin(), ids_.end(), id);
        if (it == ids_.end())
            return NotRegistered;
        ids_.erase(it);
        return Ok;
    }
    template <typename Pred>
    void prune(Pred is_alive) {
        ids_.erase(std::remove_if(ids_.begin(), ids_.end(), [&](std::int32_t id) { return !is_alive(id); }), ids_.end());
    }
    std::size_t size() const { return ids_.size(); }
    bool contains(std::int32_t id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }

private:
    std::vector<std::int32_t> ids_;
};

} // namespace power_battery
