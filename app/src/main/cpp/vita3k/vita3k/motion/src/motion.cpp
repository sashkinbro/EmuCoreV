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

#include <motion/event_handler.h>
#include <motion/functions.h>
#include <motion/state.h>

#include <ctrl/state.h>

#include <util/log.h>

#include <SDL3/SDL_gamepad.h>
#include <numbers>
#include <limits>
#include <SDL3/SDL_timer.h>

void set_display_rotation(MotionState &state, int rotation) {
    const std::lock_guard lock(state.mutex);
    state.device_native_rotation = static_cast<DeviceRotation>(rotation);
}

static void detect_device_motion_support(MotionState &state) {
    state.device_accel_id = 0;
    state.device_gyro_id = 0;

    int num_sensors;
    auto sensors_id = SDL_GetSensors(&num_sensors);
    if (!sensors_id || (num_sensors <= 0)) {
        state.has_device_motion_support = false;
        return;
    }

    for (int idx = 0; idx < num_sensors; idx++) {
        switch (SDL_GetSensorTypeForID(sensors_id[idx])) {
        case SDL_SENSOR_ACCEL:
            state.device_accel_id = sensors_id[idx];
            break;
        case SDL_SENSOR_GYRO:
            state.device_gyro_id = sensors_id[idx];
            break;
        default:
            break;
        }

        if ((state.device_accel_id != 0) && (state.device_gyro_id != 0))
            break;
    }

    SDL_free(sensors_id);

    state.has_device_motion_support = ((state.device_accel_id != 0) && (state.device_gyro_id != 0));
}

static std::string device_rotation_to_string(DeviceRotation rotation) {
    switch (rotation) {
    case ROTATION_0:
        return "ROTATION_0";
    case ROTATION_90:
        return "ROTATION_90";
    case ROTATION_180:
        return "ROTATION_180";
    case ROTATION_270:
        return "ROTATION_270";
    default:
        return "UNKNOWN_ROTATION";
    }
}

void MotionState::init() {
    const std::lock_guard lock(mutex);
    reset_runtime_locked();
    has_device_motion_support = false;
    device_accel_id = 0;
    device_gyro_id = 0;
}

void MotionState::clear_device_motion_support() {
    const std::lock_guard lock(mutex);
    stop_sensor_sampling_locked();
    has_device_motion_support = false;
    device_accel_id = 0;
    device_gyro_id = 0;
}

void MotionState::refresh_device_motion_support() {
    const std::lock_guard lock(mutex);
    stop_sensor_sampling_locked();
    has_device_motion_support = false;
    device_accel_id = device_gyro_id = 0;
    detect_device_motion_support(*this);

    if (has_device_motion_support)
        LOG_INFO("Device has a built-in accelerometer and gyroscope (native display rotation: {}).", device_rotation_to_string(device_native_rotation));
}

void MotionState::stop_sensor_sampling() {
    const std::lock_guard lock(mutex);
    stop_sensor_sampling_locked();
}

void MotionState::stop_sensor_sampling_locked() {
    is_sampling = false;
    device_accel.reset();
    device_gyro.reset();
}

void MotionState::start_sensor_sampling() {
    const std::lock_guard lock(mutex);
    start_sensor_sampling_locked();
}

void MotionState::start_sensor_sampling_locked() {
    is_sampling = true;
    if (!has_device_motion_support)
        return;

    const auto open_sensor = [](SDL_SensorPtr &sensor, uint32_t sensor_id, const char *name) {
        if (sensor_id == 0) {
            LOG_ERROR("No {} sensor ID available to open.", name);
            return false;
        }

        SDL_Sensor *raw_sensor = SDL_OpenSensor(sensor_id);
        if (!raw_sensor) {
            LOG_ERROR("Failed to open {} sensor with ID {}: {}", name, sensor_id, SDL_GetError());
            return false;
        }

        sensor.reset(raw_sensor);
        return true;
    };

    if (!open_sensor(device_accel, device_accel_id, "accelerometer") || !open_sensor(device_gyro, device_gyro_id, "gyroscope")) {
        device_accel.reset();
        device_gyro.reset();
        has_device_motion_support = false;
        // Guest sampling stays enabled with neutral readings if hardware is unavailable.
    }
}

void MotionState::reset_runtime() {
    const std::lock_guard lock(mutex);
    reset_runtime_locked();
}

void MotionState::reset_runtime_locked() {
    stop_sensor_sampling_locked();
    motion_data.ResetQuaternion();
    motion_data.ResetRotations();
    last_counter = 0;
    last_gyro_timestamp = 0;
    last_accel_timestamp = 0;
    last_updated_gyro_timestamp = 0;
    last_updated_accel_timestamp = 0;
    gyro_clock = {};
    accel_clock = {};
    minimum_event_timestamp = 0;
}

uint64_t MotionState::SensorClock::map(uint64_t raw, uint64_t latest) {
    if (!anchored || raw < raw_base) {
        anchored = true;
        raw_base = raw;
        logical_base = latest;
    }
    const auto delta = raw - raw_base;
    const uint64_t mapped = delta > std::numeric_limits<uint64_t>::max() - logical_base
        ? std::numeric_limits<uint64_t>::max() : logical_base + delta;
    return std::max(latest, mapped);
}

MotionState::Snapshot MotionState::capture_snapshot() const {
    const std::lock_guard lock(mutex);
    return { is_sampling, last_counter, last_gyro_timestamp, last_accel_timestamp,
        last_updated_gyro_timestamp, last_updated_accel_timestamp, motion_data.capture_snapshot() };
}

bool MotionState::validate_snapshot(const Snapshot &s, std::string &error) {
    if (s.last_updated_gyro_timestamp < s.last_gyro_timestamp || s.last_updated_accel_timestamp < s.last_accel_timestamp
        || s.last_gyro_timestamp == UINT64_MAX || s.last_accel_timestamp == UINT64_MAX
        || s.last_updated_gyro_timestamp == UINT64_MAX || s.last_updated_accel_timestamp == UINT64_MAX) {
        error = "invalid motion sample clock";
        return false;
    }
    return MotionInput::validate_snapshot(s.data, error);
}

bool MotionState::apply_snapshot(const Snapshot &s, std::string &error) {
    if (!validate_snapshot(s, error))
        return false;
    const std::lock_guard lock(mutex);
    if (!s.is_sampling)
        stop_sensor_sampling_locked();
    else if (!is_sampling || (has_device_motion_support && (!device_accel || !device_gyro)))
        start_sensor_sampling_locked();
    is_sampling = s.is_sampling;
    motion_data.apply_snapshot(s.data);
    last_counter = s.last_counter;
    last_gyro_timestamp = s.last_gyro_timestamp;
    last_accel_timestamp = s.last_accel_timestamp;
    last_updated_gyro_timestamp = s.last_updated_gyro_timestamp;
    last_updated_accel_timestamp = s.last_updated_accel_timestamp;
    gyro_clock = { false, 0, 0 };
    accel_clock = { false, 0, 0 };
    minimum_event_timestamp = SDL_GetTicksNS();
    return true;
}

MotionState::Snapshot MotionState::legacy_snapshot() {
    // Old archives did not store sampling/settings. Neutral active sampling is
    // a best-effort gameplay policy, not reconstruction of missing guest state.
    Snapshot snapshot;
    snapshot.is_sampling = true;
    return snapshot;
}

SceFVector3 get_acceleration(const MotionState &state) {
    Util::Vec3f accelerometer = state.motion_data.GetAcceleration();
    return {
        accelerometer.x,
        accelerometer.y,
        accelerometer.z,
    };
}

SceFVector3 get_gyroscope(const MotionState &state) {
    Util::Vec3f gyroscope = state.motion_data.GetGyroscope() * 2.f * std::numbers::pi_v<float>;
    return {
        gyroscope.x,
        gyroscope.y,
        gyroscope.z,
    };
}

Util::Quaternion<SceFloat> get_orientation(const MotionState &state) {
    auto quat = state.motion_data.GetOrientation();
    return {
        { -quat.xyz[1], -quat.w, quat.xyz[0] },
        -quat.xyz[2],
    };
}

SceBool get_gyro_bias_correction(const MotionState &state) {
    return state.motion_data.IsGyroBiasEnabled();
}

void set_gyro_bias_correction(MotionState &state, SceBool setValue) {
    state.motion_data.EnableGyroBias(setValue);
}

SceBool get_tilt_correction(MotionState &state) {
    return state.motion_data.IsTiltCorrectionEnabled();
}

void set_tilt_correction(MotionState &state, SceBool setValue) {
    state.motion_data.EnableTiltCorrection(setValue);
}

SceBool get_deadband(MotionState &state) {
    return state.motion_data.IsDeadbandEnabled();
}

void set_deadband(MotionState &state, SceBool setValue) {
    state.motion_data.EnableDeadband(setValue);
}

SceFloat get_angle_threshold(const MotionState &state) {
    return state.motion_data.GetAngleThreshold();
}

void set_angle_threshold(MotionState &state, SceFloat setValue) {
    state.motion_data.SetAngleThreshold(setValue);
}

SceFVector3 get_basic_orientation(const MotionState &state) {
    return state.motion_data.GetBasicOrientation();
}

constexpr uint64_t to_microseconds(uint64_t ns) {
    return ns / 1000;
}

template <typename SensorEvent>
static void handle_motion_event(EmuEnvState &emuenv, int32_t sensor_type, const SensorEvent &sensor) {
    const std::lock_guard lock(emuenv.motion.mutex);
    if (sensor.timestamp < emuenv.motion.minimum_event_timestamp)
        return;
    if (!emuenv.motion.is_sampling)
        return;

    if (!emuenv.ctrl.has_motion_support && !emuenv.motion.has_device_motion_support)
        return;

    const auto get_processed_sensor_data = [&]() {
        Util::Vec3f data = { sensor.data[0], sensor.data[1], sensor.data[2] };
        const auto from_gamepad = sensor.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
        if (!from_gamepad) {
            switch (emuenv.motion.device_native_rotation) {
            case ROTATION_90: // portrait -> landscape left
                std::tie(data.x, data.y, data.z) = std::make_tuple(-data.y, data.x, data.z);
                break;
            case ROTATION_180: // upside-down
                data.x *= -1.f;
                data.y *= -1.f;
                break;
            case ROTATION_270: // portrait -> landscape right
                std::tie(data.x, data.y, data.z) = std::make_tuple(data.y, -data.x, data.z);
                break;
            default: // ROTATION_0 or unknown: no transform
                break;
            }
        } else
            std::tie(data.x, data.y, data.z) = std::make_tuple(data.x, -data.z, data.y);

        return data;
    };

    const uint64_t sensor_timestamp = (sensor.sensor_timestamp > 0)
        ? to_microseconds(sensor.sensor_timestamp) // convert ns -> us
        : std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

    auto sensor_data = get_processed_sensor_data();

    if (sensor_type == SDL_SENSOR_ACCEL) {
        sensor_data /= -SDL_STANDARD_GRAVITY;
        emuenv.motion.motion_data.SetAcceleration(sensor_data);
        emuenv.motion.last_updated_accel_timestamp = emuenv.motion.accel_clock.map(sensor_timestamp, emuenv.motion.last_updated_accel_timestamp);
    } else if (sensor_type == SDL_SENSOR_GYRO) {
        sensor_data /= 2.f * std::numbers::pi_v<float>;
        emuenv.motion.motion_data.SetGyroscope(sensor_data);
        emuenv.motion.last_updated_gyro_timestamp = emuenv.motion.gyro_clock.map(sensor_timestamp, emuenv.motion.last_updated_gyro_timestamp);
    }
}

void handle_motion_event(EmuEnvState &emuenv, int32_t sensor_type, const SDL_SensorEvent &sensor) {
    handle_motion_event<SDL_SensorEvent>(emuenv, sensor_type, sensor);
}

void handle_motion_event(EmuEnvState &emuenv, int32_t sensor_type, const SDL_GamepadSensorEvent &sensor) {
    handle_motion_event<SDL_GamepadSensorEvent>(emuenv, sensor_type, sensor);
}

void refresh_motion(MotionState &state, CtrlState &ctrl_state) {
    const std::lock_guard lock(state.mutex);
    if (!state.is_sampling)
        return;

    if (!ctrl_state.has_motion_support && !state.has_device_motion_support)
        return;

    state.motion_data.UpdateOrientation(state.last_updated_accel_timestamp - state.last_accel_timestamp);
    state.motion_data.UpdateBasicOrientation();
    state.motion_data.UpdateRotation(state.last_updated_gyro_timestamp - state.last_gyro_timestamp);

    state.last_accel_timestamp = state.last_updated_accel_timestamp;
    state.last_gyro_timestamp = state.last_updated_gyro_timestamp;

    state.last_counter++;
}
