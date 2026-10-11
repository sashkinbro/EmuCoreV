// Pure motion codec. Device handles, SDL clocks and host configuration stay live.
#pragma once
#include <emucorev/savestate/state_io.h>
#include <motion/state.h>

namespace emucorev::savestate {
inline void write_motion_snapshot(BufferWriter &writer, const MotionState::Snapshot &snapshot) {
    writer.u32(1);
    writer.boolean(snapshot.is_sampling);
    writer.u32(snapshot.last_counter);
    writer.u64(snapshot.last_gyro_timestamp);
    writer.u64(snapshot.last_accel_timestamp);
    writer.u64(snapshot.last_updated_gyro_timestamp);
    writer.u64(snapshot.last_updated_accel_timestamp);
    const auto &s = snapshot.data;
    const auto vec = [&](const auto &v) { writer.value(v.x); writer.value(v.y); writer.value(v.z); };
    writer.value(s.kp); writer.value(s.ki); writer.value(s.kd);
    vec(s.real_error); vec(s.integral_error); vec(s.derivative_error);
    vec(s.quat.xyz); writer.value(s.quat.w);
    vec(s.rotations); vec(s.accel); vec(s.gyro); vec(s.gyro_bias);
    writer.value(s.gyro_deadband); writer.value(s.gyro_deadband2); writer.value(s.angle_threshold);
    vec(s.basic_orientation); vec(s.basic_orientation_base); writer.value(s.reset_counter);
    writer.boolean(s.reset_enabled); writer.boolean(s.bias_enabled); writer.boolean(s.deadband_enabled);
    writer.boolean(s.tilt_correction_enabled); writer.boolean(s.only_accelerometer);
}

inline bool read_motion_snapshot(BufferReader &reader, MotionState::Snapshot &snapshot, std::string &error) {
    if (reader.u32() != 1) {
        error = "unsupported motion input state";
        return false;
    }
    MotionState::Snapshot staged;
    staged.is_sampling = reader.boolean();
    staged.last_counter = reader.u32();
    staged.last_gyro_timestamp = reader.u64();
    staged.last_accel_timestamp = reader.u64();
    staged.last_updated_gyro_timestamp = reader.u64();
    staged.last_updated_accel_timestamp = reader.u64();
    auto &s = staged.data;
    const auto vec = [&](auto &v) { v.x = reader.value<float>(); v.y = reader.value<float>(); v.z = reader.value<float>(); };
    s.kp = reader.value<float>(); s.ki = reader.value<float>(); s.kd = reader.value<float>();
    vec(s.real_error); vec(s.integral_error); vec(s.derivative_error);
    vec(s.quat.xyz); s.quat.w = reader.value<float>();
    vec(s.rotations); vec(s.accel); vec(s.gyro); vec(s.gyro_bias);
    s.gyro_deadband = reader.value<float>(); s.gyro_deadband2 = reader.value<float>(); s.angle_threshold = reader.value<float>();
    vec(s.basic_orientation); vec(s.basic_orientation_base); s.reset_counter = reader.value<float>();
    s.reset_enabled = reader.boolean(); s.bias_enabled = reader.boolean(); s.deadband_enabled = reader.boolean();
    s.tilt_correction_enabled = reader.boolean(); s.only_accelerometer = reader.boolean();
    if (!reader.ok() || !MotionState::validate_snapshot(staged, error)) {
        if (error.empty()) error = "invalid motion input state";
        return false;
    }
    snapshot = staged;
    return true;
}
} // namespace emucorev::savestate
