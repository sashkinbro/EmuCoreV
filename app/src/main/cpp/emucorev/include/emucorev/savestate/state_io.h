#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace emucorev::savestate {

class BufferWriter {
public:
    void append(const void *data, size_t size) {
        if (size == 0)
            return;
        const size_t offset = data_.size();
        data_.resize(offset + size);
        std::memcpy(data_.data() + offset, data, size);
    }

    template <typename T>
    void value(const T &v) {
        static_assert(std::is_trivially_copyable_v<T>, "value() requires a trivially copyable type");
        append(&v, sizeof(T));
    }

    void boolean(bool v) {
        const uint8_t raw = v ? 1 : 0;
        append(&raw, 1);
    }

    void u8(uint8_t v) { append(&v, 1); }
    void u16(uint16_t v) { append(&v, 2); }
    void u32(uint32_t v) { append(&v, 4); }
    void u64(uint64_t v) { append(&v, 8); }
    void i32(int32_t v) { append(&v, 4); }
    void i64(int64_t v) { append(&v, 8); }

    void str(const std::string &s) {
        u32(static_cast<uint32_t>(s.size()));
        append(s.data(), s.size());
    }

    template <typename T, typename Fn>
    void list(const std::vector<T> &items, Fn &&write_item) {
        u32(static_cast<uint32_t>(items.size()));
        for (const T &item : items)
            write_item(item);
    }

    std::vector<uint8_t> &data() { return data_; }
    const std::vector<uint8_t> &data() const { return data_; }
    size_t size() const { return data_.size(); }
    void clear() { data_.clear(); }

private:
    std::vector<uint8_t> data_;
};

class BufferReader {
public:
    BufferReader(const void *data, size_t size)
        : data_(static_cast<const uint8_t *>(data))
        , size_(size) {
    }

    bool ok() const { return ok_; }
    size_t remaining() const { return ok_ ? (size_ - offset_) : 0; }

    bool bytes(void *dst, size_t size) {
        if (!ok_ || offset_ + size > size_) {
            ok_ = false;
            return false;
        }
        std::memcpy(dst, data_ + offset_, size);
        offset_ += size;
        return true;
    }

    template <typename T>
    T value() {
        static_assert(std::is_trivially_copyable_v<T>, "value() requires a trivially copyable type");
        T out{};
        if (!bytes(&out, sizeof(T)))
            return T{};
        return out;
    }

    bool boolean() {
        uint8_t raw = 0;
        if (!bytes(&raw, 1))
            return false;
        return raw != 0;
    }

    uint8_t u8() { return value<uint8_t>(); }
    uint16_t u16() { return value<uint16_t>(); }
    uint32_t u32() { return value<uint32_t>(); }
    uint64_t u64() { return value<uint64_t>(); }
    int32_t i32() { return value<int32_t>(); }
    int64_t i64() { return value<int64_t>(); }

    std::string str() {
        const uint32_t length = u32();
        if (!ok_ || offset_ + length > size_) {
            ok_ = false;
            return {};
        }
        std::string out(reinterpret_cast<const char *>(data_ + offset_), length);
        offset_ += length;
        return out;
    }

    template <typename T, typename Fn>
    std::vector<T> list(Fn &&read_item) {
        const uint32_t count = u32();
        std::vector<T> out;
        if (!ok_ || static_cast<size_t>(count) > remaining() + 1) {
            ok_ = false;
            return out;
        }
        out.reserve(count < 4096 ? count : 4096);
        for (uint32_t i = 0; i < count && ok_; i++)
            out.push_back(read_item());
        return out;
    }

private:
    const uint8_t *data_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
    bool ok_ = true;
};

} // namespace emucorev::savestate
