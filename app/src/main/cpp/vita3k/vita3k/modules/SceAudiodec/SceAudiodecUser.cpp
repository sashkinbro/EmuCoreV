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

#include "SceAudiodecUser.h"
#include "state_snapshot.h"

#include <audio/state.h>
#include <codec/state.h>
#include <kernel/state.h>
#include <util/lock_and_find.h>
#include <util/tracy.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <vector>

TRACY_MODULE_NAME(SceAudiodecUser);

enum {
    SCE_AUDIODEC_ERROR_API_FAIL = 0x807F0000,
    SCE_AUDIODEC_ERROR_INVALID_TYPE = 0x807F0001,
    SCE_AUDIODEC_ERROR_NOT_INITIALIZED = 0x807F0005,
    SCE_AUDIODEC_ERROR_INVALID_PTR = 0x807F0008,
    SCE_AUDIODEC_ERROR_INVALID_HANDLE = 0x807F0009,
    SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE = 0x807F000A,
    SCE_AUDIODEC_ERROR_INVALID_SIZE = 0x807F000D,
    SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG = 0x807F2000,
    SCE_AUDIODEC_MP3_ERROR_INVALID_MPEG_VERSION = 0x807F2801,
};

enum {
    SCE_AUDIODEC_MP3_MPEG_VERSION_2_5,
    SCE_AUDIODEC_MP3_MPEG_VERSION_RESERVED,
    SCE_AUDIODEC_MP3_MPEG_VERSION_2,
    SCE_AUDIODEC_MP3_MPEG_VERSION_1,
};

typedef std::shared_ptr<DecoderState> DecoderPtr;
typedef std::map<SceUID, DecoderPtr> DecoderStates;
typedef std::set<SceUID> CodecDecoders;
typedef std::map<SceAudiodecCodec, CodecDecoders> CodecDecodersMap;

enum class ReplayOp : uint8_t { Send = 1, ReceiveSize = 2, ReceiveOutput = 3, Flush = 4 };

struct ReplayEvent {
    ReplayOp op = ReplayOp::Send;
    bool input_was_null = false;
    bool succeeded = false;
    uint32_t es_size = 0;
    uint32_t samples = 0;
    std::vector<uint8_t> bytes;
};

struct DecoderRecord {
    SceAudiodecCodec codec = SCE_AUDIODEC_TYPE_AT9;
    std::array<uint32_t, 4> config{};
    bool snapshotable = true;
    size_t journal_bytes = 0;
    std::vector<ReplayEvent> journal;
    Atrac9DecoderSavedState at9_history{};
    uint32_t at9_es_size_used = 0;
    int32_t at9_superframe_frame_idx = 0;
    int32_t at9_superframe_data_left = 0;
    std::vector<uint8_t> at9_result;
};
using DecoderRecords = std::map<SceUID, DecoderRecord>;

struct AudiodecState {
    std::mutex mutex;
    DecoderStates decoders;
    CodecDecodersMap codecs;
    DecoderRecords records;
};

constexpr uint32_t SCE_AUDIODEC_AT9_MAX_ES_SIZE = 1024;
constexpr uint32_t SCE_AUDIODEC_MP3_MAX_ES_SIZE = 1441;
// max size is 1792 for AAC ES if adts is enabled
constexpr uint32_t SCE_AUDIODEC_AAC_MAX_ES_SIZE = 1536;
constexpr uint32_t SCE_AUDIODEC_CELP_MAX_ES_SIZE = 27;

// this value is multiplied by 2 if sbr is enabled
constexpr uint32_t SCE_AUDIODEC_AAC_MAX_PCM_SIZE = KiB(2);
constexpr uint32_t SCE_AUDIODEC_MP3_V1_MAX_PCM_SIZE = 2304;
constexpr uint32_t SCE_AUDIODEC_MP3_V2_MAX_PCM_SIZE = 1152;

namespace {
constexpr uint32_t kSnapshotVersion = 1;
constexpr size_t kMaxSnapshotBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxDecoderCount = 1024;
constexpr uint32_t kMaxJournalEvents = 1'000'000;
constexpr uint32_t kMaxReplayInputBytes = 16u * 1024u * 1024u;
constexpr uint32_t kMaxScratchOutputBytes = 64u * 1024u;

class SnapshotWriter {
public:
    bool u8(uint8_t value) { return append(&value, sizeof(value)); }
    bool u16(uint16_t value) { return unsigned_value(value); }
    bool u32(uint32_t value) { return unsigned_value(value); }
    bool i32(int32_t value) { return u32(static_cast<uint32_t>(value)); }
    bool u64(uint64_t value) { return unsigned_value(value); }
    bool f64(double value) { return u64(std::bit_cast<uint64_t>(value)); }
    bool bytes(const uint8_t *data, size_t size) { return append(data, size); }
    const std::vector<uint8_t> &data() const { return bytes_; }

private:
    template <typename T>
    bool unsigned_value(T value) {
        for (size_t i = 0; i < sizeof(T); ++i) {
            const uint8_t byte = static_cast<uint8_t>(value >> (i * 8));
            if (!append(&byte, 1)) return false;
        }
        return true;
    }
    bool append(const void *data, size_t size) {
        if (size > kMaxSnapshotBytes - bytes_.size()) return false;
        const auto *first = static_cast<const uint8_t *>(data);
        if (size) bytes_.insert(bytes_.end(), first, first + size);
        return true;
    }
    std::vector<uint8_t> bytes_;
};

class SnapshotReader {
public:
    explicit SnapshotReader(const std::vector<uint8_t> &bytes)
        : bytes_(bytes) {}
    bool u8(uint8_t &value) { return read(&value, 1); }
    bool u16(uint16_t &value) { return unsigned_value(value); }
    bool u32(uint32_t &value) { return unsigned_value(value); }
    bool i32(int32_t &value) {
        uint32_t bits = 0;
        if (!u32(bits)) return false;
        value = static_cast<int32_t>(bits);
        return true;
    }
    bool u64(uint64_t &value) { return unsigned_value(value); }
    bool f64(double &value) {
        uint64_t bits = 0;
        if (!u64(bits)) return false;
        value = std::bit_cast<double>(bits);
        return true;
    }
    bool bytes(std::vector<uint8_t> &out, uint32_t size) {
        if (size > remaining()) return false;
        out.assign(bytes_.begin() + offset_, bytes_.begin() + offset_ + size);
        offset_ += size;
        return true;
    }
    size_t remaining() const { return bytes_.size() - offset_; }
    bool done() const { return offset_ == bytes_.size(); }

private:
    template <typename T>
    bool unsigned_value(T &value) {
        value = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            uint8_t byte = 0;
            if (!read(&byte, 1)) return false;
            value |= static_cast<T>(byte) << (i * 8);
        }
        return true;
    }
    bool read(void *out, size_t size) {
        if (size > remaining()) return false;
        if (size) std::memcpy(out, bytes_.data() + offset_, size);
        offset_ += size;
        return true;
    }
    const std::vector<uint8_t> &bytes_;
    size_t offset_ = 0;
};

bool supported_library(SceAudiodecCodec codec) {
    return codec == SCE_AUDIODEC_TYPE_AT9 || codec == SCE_AUDIODEC_TYPE_MP3
        || codec == SCE_AUDIODEC_TYPE_AAC || codec == SCE_AUDIODEC_TYPE_CELP;
}

bool valid_decoder_config(const DecoderRecord &record) {
    switch (record.codec) {
    case SCE_AUDIODEC_TYPE_AT9: {
        const uint32_t config = record.config[0];
        return record.config[1] == 0 && record.config[2] == 0 && record.config[3] == 0
            && (config & 0xFF) == 0xFE && ((config >> 9) & 7) <= 5 && (config & 0x100) == 0;
    }
    case SCE_AUDIODEC_TYPE_MP3:
        return (record.config[0] == 1 || record.config[0] == 2)
            && record.config[1] <= SCE_AUDIODEC_MP3_MPEG_VERSION_1
            && record.config[1] != SCE_AUDIODEC_MP3_MPEG_VERSION_RESERVED
            && record.config[2] == 0 && record.config[3] == 0;
    case SCE_AUDIODEC_TYPE_AAC:
        return record.config[0] <= 1 && (record.config[1] == 1 || record.config[1] == 2)
            && (record.config[2] == 7350 || record.config[2] == 8000 || record.config[2] == 11025
                || record.config[2] == 12000 || record.config[2] == 16000 || record.config[2] == 22050
                || record.config[2] == 24000 || record.config[2] == 32000 || record.config[2] == 44100
                || record.config[2] == 48000 || record.config[2] == 64000 || record.config[2] == 88200
                || record.config[2] == 96000)
            && record.config[3] <= 1;
    default:
        return false;
    }
}

bool valid_at9_history(const DecoderRecord &record, size_t expected_result_size) {
    const uint32_t config = record.config[0];
    const auto &history = record.at9_history;
    if (record.at9_result.empty() || record.at9_result.size() > kMaxScratchOutputBytes
        || (expected_result_size && record.at9_result.size() != expected_result_size)
        || record.at9_es_size_used > 16384)
        return false;
    const int frames_per_superframe = 1 << ((config >> 27) & 3);
    const int frame_bytes = (((config >> 16) & 0xFF) << 3 | ((config >> 29) & 7)) + 1;
    if (record.at9_superframe_frame_idx < 0 || record.at9_superframe_frame_idx >= frames_per_superframe
        || record.at9_superframe_data_left < 0 || record.at9_superframe_data_left > frame_bytes * frames_per_superframe
        || (history.has_history && record.at9_superframe_data_left == 0))
        return false;
    if (history.frame_index < 0 || history.frame_index >= frames_per_superframe
        || history.superframe_frame_index < 0 || history.superframe_frame_index >= frames_per_superframe
        || history.superframe_data_left < 0 || history.superframe_data_left > frame_bytes * frames_per_superframe
        || (history.has_history && history.superframe_data_left == 0))
        return false;
    if (history.frame_index != record.at9_superframe_frame_idx
        || history.superframe_frame_index != record.at9_superframe_frame_idx
        || history.superframe_data_left != record.at9_superframe_data_left)
        return false;
    for (const auto &channel : history.prev_values)
        for (double sample : channel)
            if (!std::isfinite(sample)) return false;
    for (const auto &channel : history.channels)
        for (int32_t value : channel.scale_factors_prev)
            if (value < 0 || value > 31) return false;
    for (const auto &block : history.blocks) {
        if (block.band_count < 0 || block.band_count > 18 || block.stereo_band < 0 || block.stereo_band > 18
            || block.extension_band < 0 || block.extension_band > 18 || block.quantization_unit_count < 0 || block.quantization_unit_count > 30
            || block.stereo_quantization_unit < 0 || block.stereo_quantization_unit > 30 || block.extension_unit < 0 || block.extension_unit > 30
            || block.quantization_units_prev < 0 || block.quantization_units_prev > 30)
            return false;
    }
    return true;
}

bool append_journal(DecoderRecord &record, ReplayEvent event) {
    if (record.codec == SCE_AUDIODEC_TYPE_AT9 || !record.snapshotable)
        return true;
    const size_t event_size = 16 + event.bytes.size();
    if (record.journal.size() >= kMaxJournalEvents || event_size > kMaxSnapshotBytes - record.journal_bytes) {
        record.snapshotable = false;
        record.journal.clear();
        record.journal_bytes = 0;
        return false;
    }
    record.journal_bytes += event_size;
    record.journal.push_back(std::move(event));
    return true;
}

void journal_send(DecoderRecord *record, const uint8_t *data, uint32_t size, bool succeeded, uint32_t es_size) {
    if (!record || record->codec == SCE_AUDIODEC_TYPE_AT9) return;
    ReplayEvent event;
    event.op = ReplayOp::Send;
    event.input_was_null = data == nullptr;
    event.succeeded = succeeded;
    event.es_size = succeeded ? es_size : 0;
    if (size > kMaxReplayInputBytes || (size && !data)) {
        record->snapshotable = false;
        record->journal.clear();
        record->journal_bytes = 0;
        return;
    }
    if (size) event.bytes.assign(data, data + size);
    append_journal(*record, std::move(event));
}

void journal_receive(DecoderRecord *record, bool output, bool succeeded, uint32_t samples) {
    if (!record || record->codec == SCE_AUDIODEC_TYPE_AT9) return;
    ReplayEvent event;
    event.op = output ? ReplayOp::ReceiveOutput : ReplayOp::ReceiveSize;
    event.succeeded = succeeded;
    event.samples = succeeded ? samples : 0;
    append_journal(*record, std::move(event));
}

void journal_flush(DecoderRecord *record) {
    if (!record || record->codec == SCE_AUDIODEC_TYPE_AT9) return;
    ReplayEvent event;
    event.op = ReplayOp::Flush;
    append_journal(*record, std::move(event));
}

bool replay_journal(DecoderState &decoder, const DecoderRecord &record, std::string &error) {
    std::array<uint8_t, kMaxScratchOutputBytes> scratch{};
    for (const ReplayEvent &event : record.journal) {
        bool succeeded = false;
        uint32_t samples = 0;
        switch (event.op) {
        case ReplayOp::Send: {
            const uint8_t *data = event.input_was_null ? nullptr : event.bytes.data();
            succeeded = decoder.send(data, static_cast<uint32_t>(event.bytes.size()));
            if (succeeded && decoder.get_es_size() != event.es_size) {
                error = "audio decoder replay consumed a different input size";
                return false;
            }
            break;
        }
        case ReplayOp::ReceiveSize: {
            DecoderSize size{};
            succeeded = decoder.receive(nullptr, &size);
            samples = size.samples;
            break;
        }
        case ReplayOp::ReceiveOutput: {
            DecoderSize size{};
            succeeded = decoder.receive(scratch.data(), &size);
            samples = size.samples;
            break;
        }
        case ReplayOp::Flush:
            decoder.flush();
            continue;
        default:
            error = "invalid audio decoder replay operation";
            return false;
        }
        if (succeeded != event.succeeded || (succeeded && samples != event.samples)) {
            error = "audio decoder replay diverged from its saved operation history";
            return false;
        }
    }
    return true;
}

DecoderPtr make_decoder(const DecoderRecord &record, std::string &error) {
    if (!valid_decoder_config(record)) {
        error = "invalid audio decoder configuration";
        return {};
    }
    switch (record.codec) {
    case SCE_AUDIODEC_TYPE_AT9: {
        auto decoder = std::make_shared<Atrac9DecoderState>(record.config[0]);
        if (!decoder->valid) {
            error = "failed to initialize saved ATRAC9 decoder";
            return {};
        }
        const size_t result_size = decoder->result.size();
        if (!valid_at9_history(record, result_size)) {
            error = "invalid saved ATRAC9 decoder history";
            return {};
        }
        decoder->load_state(&record.at9_history);
        decoder->result = record.at9_result;
        decoder->es_size_used = record.at9_es_size_used;
        decoder->superframe_frame_idx = record.at9_superframe_frame_idx;
        decoder->superframe_data_left = record.at9_superframe_data_left;
        return decoder;
    }
    case SCE_AUDIODEC_TYPE_MP3: {
        auto decoder = std::make_shared<Mp3DecoderState>(record.config[0]);
        decoder->es_size_used = 0;
        if (!replay_journal(*decoder, record, error)) return {};
        return decoder;
    }
    case SCE_AUDIODEC_TYPE_AAC: {
        auto decoder = std::make_shared<AacDecoderState>(record.config[2], record.config[1], record.config[3] != 0);
        decoder->es_size_used = 0;
        if (!replay_journal(*decoder, record, error)) return {};
        return decoder;
    }
    default:
        error = "unsupported saved audio decoder type";
        return {};
    }
}

bool write_history(SnapshotWriter &writer, const Atrac9DecoderSavedState &history) {
    if (!writer.u8(history.has_history)) return false;
    for (const auto &channel : history.prev_values)
        for (double value : channel) if (!writer.f64(value)) return false;
    for (const auto &channel : history.channels) {
        for (int32_t value : channel.scale_factors_prev) if (!writer.i32(value)) return false;
        if (!writer.u8(channel.rng_initialized)) return false;
        for (uint16_t value : channel.rng_state) if (!writer.u16(value)) return false;
    }
    for (const auto &block : history.blocks) {
        if (!writer.i32(block.band_count) || !writer.i32(block.stereo_band) || !writer.i32(block.extension_band)
            || !writer.i32(block.quantization_unit_count) || !writer.i32(block.stereo_quantization_unit)
            || !writer.i32(block.extension_unit) || !writer.i32(block.quantization_units_prev)
            || !writer.u8(block.band_extension_enabled)) return false;
    }
    return writer.i32(history.frame_index) && writer.i32(history.superframe_frame_index)
        && writer.i32(history.superframe_data_left);
}

bool read_history(SnapshotReader &reader, Atrac9DecoderSavedState &history) {
    uint8_t boolean = 0;
    if (!reader.u8(boolean) || boolean > 1) return false;
    history.has_history = boolean != 0;
    for (auto &channel : history.prev_values)
        for (double &value : channel) if (!reader.f64(value)) return false;
    for (auto &channel : history.channels) {
        for (int32_t &value : channel.scale_factors_prev) if (!reader.i32(value)) return false;
        if (!reader.u8(boolean) || boolean > 1) return false;
        channel.rng_initialized = boolean != 0;
        for (uint16_t &value : channel.rng_state) if (!reader.u16(value)) return false;
    }
    for (auto &block : history.blocks) {
        if (!reader.i32(block.band_count) || !reader.i32(block.stereo_band) || !reader.i32(block.extension_band)
            || !reader.i32(block.quantization_unit_count) || !reader.i32(block.stereo_quantization_unit)
            || !reader.i32(block.extension_unit) || !reader.i32(block.quantization_units_prev)
            || !reader.u8(boolean) || boolean > 1) return false;
        block.band_extension_enabled = boolean != 0;
    }
    return reader.i32(history.frame_index) && reader.i32(history.superframe_frame_index)
        && reader.i32(history.superframe_data_left);
}

bool parse_snapshot(const std::vector<uint8_t> &blob, CodecDecodersMap &libraries,
    DecoderRecords &records, std::string &error) {
    if (blob.size() < 16 || blob.size() > kMaxSnapshotBytes) {
        error = "audio decoder snapshot has an invalid size";
        return false;
    }
    SnapshotReader reader(blob);
    uint8_t magic[4]{};
    for (uint8_t &byte : magic) if (!reader.u8(byte)) return false;
    uint32_t version = 0, library_count = 0, decoder_count = 0;
    if (std::memcmp(magic, "ADCD", 4) != 0 || !reader.u32(version) || version != kSnapshotVersion
        || !reader.u32(library_count) || library_count > 4) {
        error = "invalid audio decoder snapshot header";
        return false;
    }
    for (uint32_t i = 0; i < library_count; ++i) {
        uint32_t raw_codec = 0;
        if (!reader.u32(raw_codec) || !supported_library(static_cast<SceAudiodecCodec>(raw_codec))
            || !libraries.emplace(static_cast<SceAudiodecCodec>(raw_codec), CodecDecoders{}).second) {
            error = "invalid audio decoder library table";
            return false;
        }
    }
    if (!reader.u32(decoder_count) || decoder_count > kMaxDecoderCount) {
        error = "invalid audio decoder handle table";
        return false;
    }
    uint32_t total_events = 0;
    for (uint32_t i = 0; i < decoder_count; ++i) {
        SceUID handle = 0;
        uint32_t raw_codec = 0;
        DecoderRecord record;
        if (!reader.i32(handle) || handle <= 0 || !reader.u32(raw_codec)) {
            error = "invalid audio decoder handle";
            return false;
        }
        record.codec = static_cast<SceAudiodecCodec>(raw_codec);
        for (uint32_t &config : record.config)
            if (!reader.u32(config)) { error = "truncated audio decoder configuration"; return false; }
        if (!valid_decoder_config(record)) {
            error = "invalid audio decoder configuration";
            return false;
        }
        if (record.codec == SCE_AUDIODEC_TYPE_AT9) {
            if (!read_history(reader, record.at9_history) || !reader.u32(record.at9_es_size_used)
                || !reader.i32(record.at9_superframe_frame_idx) || !reader.i32(record.at9_superframe_data_left)) {
                error = "truncated ATRAC9 decoder history";
                return false;
            }
            uint32_t result_size = 0;
            if (!reader.u32(result_size) || result_size > kMaxReplayInputBytes || !reader.bytes(record.at9_result, result_size)) {
                error = "invalid ATRAC9 decoder result";
                return false;
            }
        } else {
            uint32_t event_count = 0;
            if (!reader.u32(event_count) || event_count > kMaxJournalEvents - total_events) {
                error = "invalid audio decoder replay journal";
                return false;
            }
            total_events += event_count;
            record.journal.reserve(event_count);
            for (uint32_t e = 0; e < event_count; ++e) {
                ReplayEvent event;
                uint8_t op = 0, null_input = 0, succeeded = 0, reserved = 0;
                uint32_t data_size = 0;
                if (!reader.u8(op) || op < static_cast<uint8_t>(ReplayOp::Send) || op > static_cast<uint8_t>(ReplayOp::Flush)
                    || !reader.u8(null_input) || null_input > 1 || !reader.u8(succeeded) || succeeded > 1
                    || !reader.u8(reserved) || reserved != 0 || !reader.u32(event.es_size)
                    || !reader.u32(event.samples) || !reader.u32(data_size) || data_size > kMaxReplayInputBytes
                    || !reader.bytes(event.bytes, data_size)) {
                    error = "invalid audio decoder replay event";
                    return false;
                }
                event.op = static_cast<ReplayOp>(op);
                event.input_was_null = null_input != 0;
                event.succeeded = succeeded != 0;
                if (((event.op == ReplayOp::ReceiveSize || event.op == ReplayOp::ReceiveOutput || event.op == ReplayOp::Flush)
                        && (data_size != 0 || event.input_was_null))
                    || (event.op == ReplayOp::Send && (event.input_was_null || data_size == 0))
                    || (event.op == ReplayOp::Flush && (data_size != 0 || event.succeeded || event.samples != 0 || event.es_size != 0))) {
                    error = "invalid audio decoder replay event fields";
                    return false;
                }
                record.journal_bytes += 16 + data_size;
                if (record.journal_bytes > kMaxSnapshotBytes) {
                    error = "audio decoder replay journal exceeds size limit";
                    return false;
                }
                record.journal.push_back(std::move(event));
            }
        }
        if (!records.emplace(handle, std::move(record)).second) {
            error = "duplicate audio decoder handle";
            return false;
        }
    }
    if (!reader.done()) {
        error = "audio decoder snapshot has trailing data";
        return false;
    }
    for (const auto &[handle, record] : records) {
        auto library = libraries.find(record.codec);
        if (library == libraries.end()) {
            error = "audio decoder handle has no initialized library";
            return false;
        }
        library->second.insert(handle);
        if (record.codec == SCE_AUDIODEC_TYPE_AT9) {
            if (!valid_at9_history(record, 0)) {
                error = "invalid ATRAC9 decoder history";
                return false;
            }
        }
    }
    return true;
}

} // namespace

namespace audiodec {
struct PreparedState {
    DecoderStates decoders;
    CodecDecodersMap codecs;
    DecoderRecords records;
};

void initialize_state(EmuEnvState &emuenv) {
    emuenv.kernel.obj_store.create<AudiodecState>();
}

void clear_state(EmuEnvState &emuenv) {
    initialize_state(emuenv);
    auto *state = emuenv.kernel.obj_store.get<AudiodecState>();
    const std::lock_guard lock(state->mutex);
    state->decoders.clear();
    state->codecs.clear();
    state->records.clear();
}

bool validate_state(const std::vector<uint8_t> &blob, std::string &error) {
    std::shared_ptr<PreparedState> prepared;
    return prepare_state(blob, prepared, error);
}

bool prepare_state(const std::vector<uint8_t> &blob, std::shared_ptr<PreparedState> &prepared, std::string &error) {
    error.clear();
    prepared.reset();
    CodecDecodersMap libraries;
    DecoderRecords records;
    if (!parse_snapshot(blob, libraries, records, error)) return false;
    auto candidate = std::make_shared<PreparedState>();
    for (const auto &[handle, record] : records) {
        DecoderPtr decoder = make_decoder(record, error);
        if (!decoder) return false;
        candidate->decoders.emplace(handle, std::move(decoder));
    }
    candidate->codecs = std::move(libraries);
    candidate->records = std::move(records);
    prepared = std::move(candidate);
    return true;
}

std::vector<int32_t> decoder_handles(const PreparedState &prepared) {
    std::vector<int32_t> handles;
    handles.reserve(prepared.decoders.size());
    for (const auto &[handle, decoder] : prepared.decoders) {
        (void)decoder;
        handles.push_back(handle);
    }
    return handles;
}

bool capture_state(EmuEnvState &emuenv, std::vector<uint8_t> &blob, std::string &error) {
    error.clear();
    blob.clear();
    initialize_state(emuenv);
    auto *state = emuenv.kernel.obj_store.get<AudiodecState>();
    const std::lock_guard lock(state->mutex);
    SnapshotWriter writer;
    for (uint8_t byte : std::array<uint8_t, 4>{ 'A', 'D', 'C', 'D' })
        if (!writer.u8(byte)) { error = "audio decoder snapshot exceeds size limit"; return false; }
    if (state->codecs.size() > 4 || !writer.u32(kSnapshotVersion) || !writer.u32(static_cast<uint32_t>(state->codecs.size()))) {
        error = "audio decoder snapshot exceeds size limit";
        return false;
    }
    for (const auto &[codec, handles] : state->codecs) {
        if (!supported_library(codec) || !writer.u32(codec)) {
            error = "invalid initialized audio decoder library";
            return false;
        }
    }
    if (state->decoders.size() > kMaxDecoderCount || !writer.u32(static_cast<uint32_t>(state->decoders.size()))) {
        error = "too many audio decoder handles";
        return false;
    }
    for (const auto &[handle, decoder] : state->decoders) {
        const auto record_it = state->records.find(handle);
        if (!decoder || record_it == state->records.end() || !record_it->second.snapshotable) {
            error = "audio decoder has no complete replay history";
            return false;
        }
        const DecoderRecord *record = &record_it->second;
        DecoderRecord at9_snapshot;
        if (record->codec == SCE_AUDIODEC_TYPE_AT9) {
            auto at9 = std::dynamic_pointer_cast<Atrac9DecoderState>(decoder);
            if (!at9 || !at9->valid) { error = "invalid live ATRAC9 decoder"; return false; }
            at9_snapshot = *record;
            at9->export_state(&at9_snapshot.at9_history);
            at9_snapshot.at9_es_size_used = at9->es_size_used;
            at9_snapshot.at9_superframe_frame_idx = at9->superframe_frame_idx;
            at9_snapshot.at9_superframe_data_left = at9->superframe_data_left;
            at9_snapshot.at9_result = at9->result;
            if (!valid_at9_history(at9_snapshot, at9->result.size())) { error = "invalid live ATRAC9 decoder history"; return false; }
            record = &at9_snapshot;
        }
        if (!writer.i32(handle) || !writer.u32(record->codec)) { error = "audio decoder snapshot exceeds size limit"; return false; }
        for (uint32_t config : record->config)
            if (!writer.u32(config)) { error = "audio decoder snapshot exceeds size limit"; return false; }
        if (record->codec == SCE_AUDIODEC_TYPE_AT9) {
            if (!write_history(writer, record->at9_history) || !writer.u32(record->at9_es_size_used)
                || !writer.i32(record->at9_superframe_frame_idx) || !writer.i32(record->at9_superframe_data_left)
                || !writer.u32(static_cast<uint32_t>(record->at9_result.size()))
                || !writer.bytes(record->at9_result.data(), record->at9_result.size())) {
                error = "audio decoder snapshot exceeds size limit";
                return false;
            }
        } else {
            if (record->journal.size() > kMaxJournalEvents || !writer.u32(static_cast<uint32_t>(record->journal.size()))) {
                error = "audio decoder replay journal exceeds size limit";
                return false;
            }
            for (const ReplayEvent &event : record->journal) {
                if (!writer.u8(static_cast<uint8_t>(event.op)) || !writer.u8(event.input_was_null)
                    || !writer.u8(event.succeeded) || !writer.u8(0) || !writer.u32(event.es_size)
                    || !writer.u32(event.samples) || !writer.u32(static_cast<uint32_t>(event.bytes.size()))
                    || !writer.bytes(event.bytes.data(), event.bytes.size())) {
                    error = "audio decoder replay journal exceeds size limit";
                    return false;
                }
            }
        }
    }
    blob = writer.data();
    return true;
}

bool restore_state(EmuEnvState &emuenv, const std::vector<uint8_t> &blob, std::string &error) {
    std::shared_ptr<PreparedState> prepared;
    if (!prepare_state(blob, prepared, error)) return false;
    apply_state(emuenv, std::move(prepared));
    return true;
}

void apply_state(EmuEnvState &emuenv, std::shared_ptr<PreparedState> prepared) {
    if (!prepared)
        return;
    initialize_state(emuenv);
    auto *state = emuenv.kernel.obj_store.get<AudiodecState>();
    const std::lock_guard lock(state->mutex);
    state->decoders.swap(prepared->decoders);
    state->codecs.swap(prepared->codecs);
    state->records.swap(prepared->records);
}

} // namespace audiodec

LIBRARY_INIT(SceAudiodec) {
    audiodec::initialize_state(emuenv);
}

EXPORT(int, sceAudiodecClearContext, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecClearContext, ctrl)

    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->codecs.empty()) {
        return SCE_AUDIODEC_ERROR_NOT_INITIALIZED;
    }
    if (!ctrl->handle) {
        return SCE_AUDIODEC_ERROR_INVALID_HANDLE;
    }

    auto decoder_it = state->decoders.find(ctrl->handle);
    if (decoder_it == state->decoders.end() || !decoder_it->second) {
        return SCE_AUDIODEC_ERROR_NOT_HANDLE_IN_USE;
    }
    decoder_it->second->flush();
    auto record = state->records.find(ctrl->handle);
    if (record != state->records.end())
        journal_flush(&record->second);

    return 0;
}

static int create_decoder(EmuEnvState &emuenv, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec) {
    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    SceAudiodecInfo *guest_info = ctrl->info.get(emuenv.mem);
    if (!guest_info)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    const SceUID handle = emuenv.kernel.get_next_uid();
    DecoderRecord record;
    record.codec = codec;
    DecoderPtr decoder;

    switch (codec) {
    case SCE_AUDIODEC_TYPE_AT9: {
        auto &info = guest_info->at9;
        record.config[0] = info.config_data;
        if (!valid_decoder_config(record))
            return SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG;
        auto at9 = std::make_shared<Atrac9DecoderState>(info.config_data);
        if (!at9->valid)
            return SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG;
        at9->es_size_used = 0;
        info.channels = at9->get(DecoderQuery::CHANNELS);
        info.bit_rate = at9->get(DecoderQuery::BIT_RATE);
        info.sample_rate = at9->get(DecoderQuery::SAMPLE_RATE);
        info.super_frame_size = at9->get(DecoderQuery::AT9_SUPERFRAME_SIZE);
        info.frames_in_super_frame = at9->get(DecoderQuery::AT9_FRAMES_IN_SUPERFRAME);
        ctrl->es_size_max = std::min(info.super_frame_size, SCE_AUDIODEC_AT9_MAX_ES_SIZE);
        ctrl->pcm_size_max = at9->get(DecoderQuery::AT9_SAMPLE_PER_FRAME)
            * at9->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
        decoder = std::move(at9);
        break;
    }
    case SCE_AUDIODEC_TYPE_AAC: {
        auto &info = guest_info->aac;
        record.config = { info.is_adts, info.channels, info.sample_rate, info.is_sbr };
        if (!valid_decoder_config(record))
            return SCE_AUDIODEC_ERROR_INVALID_SIZE;
        auto aac = std::make_shared<AacDecoderState>(info.sample_rate, info.channels, info.is_sbr != 0);
        aac->es_size_used = 0;
        ctrl->es_size_max = SCE_AUDIODEC_AAC_MAX_ES_SIZE + (info.is_adts ? 0x100 : 0);
        ctrl->pcm_size_max = info.channels * SCE_AUDIODEC_AAC_MAX_PCM_SIZE * (info.is_sbr ? 2 : 1);
        LOG_WARN_IF(info.is_adts || info.is_sbr, "report it to dev, is_adts: {}, is_sbr: {}", info.is_adts, info.is_sbr);
        decoder = std::move(aac);
        break;
    }
    case SCE_AUDIODEC_TYPE_MP3: {
        auto &info = guest_info->mp3;
        if (info.version > SCE_AUDIODEC_MP3_MPEG_VERSION_1
            || info.version == SCE_AUDIODEC_MP3_MPEG_VERSION_RESERVED)
            return SCE_AUDIODEC_MP3_ERROR_INVALID_MPEG_VERSION;
        record.config[0] = info.channels;
        record.config[1] = info.version;
        if (!valid_decoder_config(record))
            return SCE_AUDIODEC_ERROR_INVALID_SIZE;
        auto mp3 = std::make_shared<Mp3DecoderState>(info.channels);
        mp3->es_size_used = 0;
        ctrl->es_size_max = SCE_AUDIODEC_MP3_MAX_ES_SIZE;
        ctrl->pcm_size_max = info.channels * (info.version == SCE_AUDIODEC_MP3_MPEG_VERSION_1
                ? SCE_AUDIODEC_MP3_V1_MAX_PCM_SIZE : SCE_AUDIODEC_MP3_V2_MAX_PCM_SIZE);
        decoder = std::move(mp3);
        break;
    }
    default:
        LOG_ERROR("Unimplemented audio decoder {}.", codec);
        return -1;
    }

    ctrl->handle = handle;
    state->decoders.emplace(handle, std::move(decoder));
    state->records.emplace(handle, std::move(record));
    state->codecs[codec].insert(handle);
    return 0;
}

EXPORT(int, sceAudiodecCreateDecoder, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec) {
    TRACY_FUNC(sceAudiodecCreateDecoder, ctrl, codec);
    return create_decoder(emuenv, ctrl, codec);
}

EXPORT(int, sceAudiodecCreateDecoderExternal, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec, void *context, uint32_t size) {
    TRACY_FUNC(sceAudiodecCreateDecoderExternal, ctrl, codec, context, size);
    // I think context is supposed to be just extra memory where I can allocate my context.
    // I'm just going to allocate like regular sceAudiodecCreateDecoder and see how it goes.
    // Almost sure zang has already tried this so :/ - desgroup
    return create_decoder(emuenv, ctrl, codec);
}

EXPORT(int, sceAudiodecCreateDecoderResident) {
    TRACY_FUNC(sceAudiodecCreateDecoderResident);
    return UNIMPLEMENTED();
}

static int decode_audio_frames(EmuEnvState &emuenv, const char *export_name, SceAudiodecCtrl *ctrl, SceUInt32 nb_frames) {
    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    const auto decoder_it = state->decoders.find(ctrl->handle);
    if (decoder_it == state->decoders.end() || !decoder_it->second)
        return SCE_AUDIODEC_ERROR_INVALID_HANDLE;
    const DecoderPtr &decoder = decoder_it->second;

    uint8_t *es_data = ctrl->es_data.get(emuenv.mem);
    uint8_t *pcm_data = ctrl->pcm_data.get(emuenv.mem);
    if (nb_frames && (!es_data || !pcm_data))
        return SCE_AUDIODEC_ERROR_INVALID_PTR;
    auto record_it = state->records.find(ctrl->handle);
    DecoderRecord *record = record_it == state->records.end() ? nullptr : &record_it->second;

    ctrl->es_size_used = 0;
    ctrl->pcm_size_given = 0;

    for (uint32_t frame = 0; frame < nb_frames; frame++) {
        const bool sent = decoder->send(es_data, ctrl->es_size_max);
        const uint32_t used = sent ? decoder->get_es_size() : 0;
        journal_send(record, es_data, ctrl->es_size_max, sent, used);
        if (!sent) {
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
        }

        DecoderSize size{};
        const bool received = decoder->receive(pcm_data, &size);
        journal_receive(record, true, received, size.samples);
        if (!received)
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);

        uint32_t es_size_used = std::min(decoder->get_es_size(), ctrl->es_size_max);
        assert(es_size_used <= ctrl->es_size_max);
        ctrl->es_size_used += es_size_used;
        es_data += es_size_used;

        uint32_t pcm_size_given = size.samples * decoder->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
        assert(pcm_size_given <= ctrl->pcm_size_max);
        ctrl->pcm_size_given += pcm_size_given;
        pcm_data += pcm_size_given;
    }

    return 0;
}

EXPORT(int, sceAudiodecDecode, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecDecode, ctrl);
    return decode_audio_frames(emuenv, export_name, ctrl, 1);
}

EXPORT(int, sceAudiodecDecodeNFrames, SceAudiodecCtrl *ctrl, SceUInt32 nFrames) {
    TRACY_FUNC(sceAudiodecDecodeNFrames, ctrl, nFrames);
    return decode_audio_frames(emuenv, export_name, ctrl, nFrames);
}

EXPORT(int, sceAudiodecDecodeNStreams, SceAudiodecCtrl *ctrl, SceUInt32 nStreams) {
    TRACY_FUNC(sceAudiodecDecodeNStreams, ctrl, nStreams);

    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    return UNIMPLEMENTED();
}

EXPORT(int, sceAudiodecDeleteDecoder, SceAudiodecCtrl *ctrl) {
    TRACY_FUNC(sceAudiodecDeleteDecoder, ctrl);

    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->decoders.erase(ctrl->handle);
    state->records.erase(ctrl->handle);

    // there are at most 4 different codecs, we can afford to look
    // at all of them (the handle is in one of them)
    for (auto &codec : state->codecs) {
        codec.second.erase(ctrl->handle);
    }

    return 0;
}

EXPORT(int, sceAudiodecDeleteDecoderExternal, SceAudiodecCtrl *ctrl, void *context) {
    TRACY_FUNC(sceAudiodecDeleteDecoderExternal, ctrl, context);
    return CALL_EXPORT(sceAudiodecDeleteDecoder, ctrl);
}

EXPORT(int, sceAudiodecDeleteDecoderResident) {
    TRACY_FUNC(sceAudiodecDeleteDecoderResident);
    return UNIMPLEMENTED();
}

static std::uint32_t getAt9Factor(const std::uint8_t *config_data) {
    std::uint32_t value = (config_data[1] & 0xf) >> 1;
    if (value == 0)
        return 1;
    if ((value == 1) || (value == 2))
        return 2;
    return SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG;
}

EXPORT(int, sceAudiodecGetContextSize, SceAudiodecCtrl *pCtrl, SceAudiodecCodec codecType) {
    TRACY_FUNC(sceAudiodecGetContextSize, pCtrl, codecType);

    if (!pCtrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (pCtrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    switch (codecType) {
    case SCE_AUDIODEC_TYPE_AT9: {
        auto *info = pCtrl->info.get(emuenv.mem);
        if (!info)
            return SCE_AUDIODEC_ERROR_INVALID_PTR;
        const std::uint32_t at9Factor = getAt9Factor(reinterpret_cast<std::uint8_t *>(&info->at9.config_data));
        if (at9Factor == 1 || at9Factor == 2) {
            return 0x400 * at9Factor + 0x400;
        }
        return SCE_AUDIODEC_AT9_ERROR_INVALID_CONFIG;
    }
    case SCE_AUDIODEC_TYPE_AAC:
        return 0x18000;
    case SCE_AUDIODEC_TYPE_MP3:
    case SCE_AUDIODEC_TYPE_CELP:
        return 0;
    default:
        // Found these during reverse engineering, log them in case we need an implementation
        LOG_WARN_IF(codecType == 0x1007 || codecType == 0x1008, "Unsupported codec type {}", codecType);
        return SCE_AUDIODEC_ERROR_INVALID_TYPE;
    }
}

EXPORT(int, sceAudiodecGetInternalError) {
    TRACY_FUNC(sceAudiodecGetInternalError);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceAudiodecInitLibrary, SceAudiodecCodec codecType, SceAudiodecInitParam *pInitParam) {
    TRACY_FUNC(sceAudiodecInitLibrary, codecType, pInitParam);

    if (!pInitParam)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    state->codecs.try_emplace(codecType, CodecDecoders());
    return 0;
}

EXPORT(int, sceAudiodecPartlyDecode, SceAudiodecCtrl *ctrl, SceUInt32 samples_offset, SceUInt32 samples_to_decode) {
    TRACY_FUNC(sceAudiodecPartlyDecode, ctrl, samples_offset, samples_to_decode);

    if (!ctrl)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    if (ctrl->size != sizeof(SceAudiodecCtrl))
        return SCE_AUDIODEC_ERROR_INVALID_SIZE;

    // this function is only called by libatrac
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    const auto record = state->records.find(ctrl->handle);
    const auto decoder_it = state->decoders.find(ctrl->handle);
    if (record == state->records.end() || decoder_it == state->decoders.end() || !decoder_it->second)
        return SCE_AUDIODEC_ERROR_INVALID_HANDLE;
    if (record->second.codec != SCE_AUDIODEC_TYPE_AT9)
        return SCE_AUDIODEC_ERROR_INVALID_TYPE;
    const std::shared_ptr<DecoderState> &decoder = decoder_it->second;

    uint8_t *es_data = ctrl->es_data.get(emuenv.mem);
    uint8_t *pcm_data = ctrl->pcm_data.get(emuenv.mem);
    if (!es_data || !pcm_data)
        return SCE_AUDIODEC_ERROR_INVALID_PTR;

    // TODO: if the offset is too big, do not decode the first superframes (doesn't seem to happen with libatrac)
    const uint32_t bytes_per_sample = decoder->get(DecoderQuery::CHANNELS) * sizeof(int16_t);
    ctrl->es_size_used = 0;
    ctrl->pcm_size_given = 0;
    std::vector<uint8_t> temp_storage;
    temp_storage.reserve((samples_offset + samples_to_decode) * bytes_per_sample);

    while (ctrl->pcm_size_given < (samples_offset + samples_to_decode) * bytes_per_sample) {
        DecoderSize size{};
        if (!decoder->send(es_data, ctrl->es_size_max)) {
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
        }
        const uint32_t es_size_used = decoder->get_es_size();
        assert(es_size_used <= ctrl->es_size_max);
        ctrl->es_size_used += es_size_used;
        es_data += es_size_used;

        if (!decoder->receive(nullptr, &size))
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
        const uint32_t pcm_size_given = size.samples * bytes_per_sample;
        ctrl->pcm_size_given += pcm_size_given;
        const uint32_t old_size = temp_storage.size();
        temp_storage.resize(old_size + pcm_size_given);
        if (!decoder->receive(temp_storage.data() + old_size, &size))
            return RET_ERROR(SCE_AUDIODEC_ERROR_API_FAIL);
    }

    memcpy(pcm_data, temp_storage.data() + samples_offset * bytes_per_sample, samples_to_decode * bytes_per_sample);

    return 0;
}

EXPORT(SceInt32, sceAudiodecTermLibrary, SceAudiodecCodec codecType) {
    TRACY_FUNC(sceAudiodecTermLibrary, codecType);
    const auto state = emuenv.kernel.obj_store.get<AudiodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    // remove decoders associated with codecType
    auto codec = state->codecs.find(codecType);
    if (codec == state->codecs.end())
        return 0;
    for (auto &handle : codec->second) {
        state->decoders.erase(handle);
        state->records.erase(handle);
    }
    state->codecs.erase(codec);
    return 0;
}
