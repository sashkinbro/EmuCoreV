#include <emucorev/paf/resource_document.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace emucorev::paf {
namespace {
using Bytes = std::span<const std::uint8_t>;

Bytes range(Bytes bytes, std::uint64_t offset, std::uint64_t size) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        throw std::runtime_error("CXML range exceeds its table");
    return bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(size));
}

std::uint32_t word(Bytes bytes, std::size_t offset) {
    const auto b = range(bytes, offset, 4);
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8)
        | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}

std::string terminated_string(Bytes bytes, std::uint32_t offset) {
    const auto tail = range(bytes, offset, bytes.size() >= offset ? bytes.size() - offset : 0);
    const auto end = std::find(tail.begin(), tail.end(), 0);
    if (end == tail.end()) throw std::runtime_error("Unterminated CXML string");
    return {tail.begin(), end};
}

void require_type(const Attribute &attribute, AttributeType type) {
    if (attribute.type != type) throw std::runtime_error("CXML attribute type mismatch");
}
}

ResourceDocument::ResourceDocument(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    const Bytes image(bytes_);
    range(image, 0, 80);
    magic_.assign(bytes_.begin(), bytes_.begin() + 4);
    if (magic_ != "RCOF" && magic_ != "CXML") throw std::runtime_error("Unsupported CXML magic");
    version_ = word(image, 4);
    if (version_ != 0x110) throw std::runtime_error("Unsupported CXML version");
    for (std::size_t i = 0; i < 9; ++i) {
        tables_[i] = {word(image, 8 + i * 8), word(image, 12 + i * 8)};
        range(image, tables_[i].offset, tables_[i].size);
        if (tables_[i].size && tables_[i].offset < 80)
            throw std::runtime_error("CXML table overlaps header");
        for (std::size_t j = 0; j < i; ++j) {
            const auto &a = tables_[i]; const auto &b = tables_[j];
            if (a.size && b.size && std::uint64_t(a.offset) < std::uint64_t(b.offset) + b.size
                && std::uint64_t(b.offset) < std::uint64_t(a.offset) + a.size)
                throw std::runtime_error("Overlapping CXML tables");
        }
    }
    const auto tree = section(Section::tree);
    for (std::uint64_t position = 0; position < tree.size();) {
        range(tree, position, 28);
        const auto p = static_cast<std::size_t>(position);
        const auto count = word(tree, p + 4);
        const std::uint64_t record_size = 28 + std::uint64_t(count) * 16;
        range(tree, position, record_size);
        Element element{static_cast<std::uint32_t>(position),
            terminated_string(section(Section::strings), word(tree, p)),
            std::bit_cast<std::int32_t>(word(tree, p + 8)),
            std::bit_cast<std::int32_t>(word(tree, p + 12)),
            std::bit_cast<std::int32_t>(word(tree, p + 16)),
            std::bit_cast<std::int32_t>(word(tree, p + 20)),
            std::bit_cast<std::int32_t>(word(tree, p + 24)), {}};
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto a = p + 28 + std::size_t(i) * 16;
            Attribute attribute{terminated_string(section(Section::strings), word(tree, a)),
                static_cast<AttributeType>(word(tree, a + 4)), word(tree, a + 8), word(tree, a + 12)};
            validate_attribute(attribute);
            element.attributes.push_back(std::move(attribute));
        }
        elements_.push_back(std::move(element));
        position += record_size;
    }
    validate_tree();
}

std::span<const std::uint8_t> ResourceDocument::section(Section kind) const {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= 9) throw std::runtime_error("Invalid CXML table");
    return range(bytes_, tables_[index].offset, tables_[index].size);
}

const Element &ResourceDocument::element_at(std::int32_t offset) const {
    if (offset < 0) throw std::runtime_error("Null CXML element reference");
    const auto it = std::lower_bound(elements_.begin(), elements_.end(), std::uint32_t(offset),
        [](const Element &element, std::uint32_t value) { return element.offset < value; });
    if (it == elements_.end() || it->offset != std::uint32_t(offset))
        throw std::runtime_error("CXML link does not point to an element");
    return *it;
}

std::string ResourceDocument::string_value(const Attribute &a) const {
    require_type(a, AttributeType::string);
    const auto bytes = range(section(Section::strings), a.first, a.second);
    return {bytes.begin(), bytes.end()};
}

std::u16string ResourceDocument::wide_string_value(const Attribute &a) const {
    require_type(a, AttributeType::wide_string);
    const auto bytes = range(section(Section::wide_strings), std::uint64_t(a.first) * 2, std::uint64_t(a.second) * 2);
    std::u16string result;
    for (std::size_t i = 0; i < bytes.size(); i += 2)
        result.push_back(char16_t(std::uint16_t(bytes[i]) | (std::uint16_t(bytes[i + 1]) << 8)));
    return result;
}

std::vector<std::int32_t> ResourceDocument::integer_values(const Attribute &a) const {
    require_type(a, AttributeType::integers);
    const auto bytes = range(section(Section::integers), std::uint64_t(a.first) * 4, std::uint64_t(a.second) * 4);
    std::vector<std::int32_t> result;
    for (std::size_t i = 0; i < bytes.size(); i += 4) result.push_back(std::bit_cast<std::int32_t>(word(bytes, i)));
    return result;
}

std::vector<float> ResourceDocument::real_values(const Attribute &a) const {
    require_type(a, AttributeType::reals);
    const auto bytes = range(section(Section::reals), std::uint64_t(a.first) * 4, std::uint64_t(a.second) * 4);
    std::vector<float> result;
    for (std::size_t i = 0; i < bytes.size(); i += 4) result.push_back(std::bit_cast<float>(word(bytes, i)));
    return result;
}

std::span<const std::uint8_t> ResourceDocument::file_value(const Attribute &a) const {
    require_type(a, AttributeType::file);
    return range(section(Section::files), a.first, a.second);
}

std::uint32_t ResourceDocument::hash_value(const Attribute &a) const {
    require_type(a, AttributeType::hash);
    return word(range(section(Section::hashes), std::uint64_t(a.first) * 4, 4), 0);
}

Reference ResourceDocument::reference_value(const Attribute &a) const {
    if (a.type != AttributeType::id && a.type != AttributeType::id_reference
        && a.type != AttributeType::id_hash && a.type != AttributeType::id_hash_reference)
        throw std::runtime_error("CXML attribute is not a reference");
    if (a.first == std::numeric_limits<std::uint32_t>::max()) return {-1, {}, 0};
    if (a.type == AttributeType::id || a.type == AttributeType::id_reference) {
        const auto table = section(Section::ids);
        range(table, a.first, 5);
        return {std::bit_cast<std::int32_t>(word(table, a.first)), terminated_string(table, a.first + 4), 0};
    }
    if (a.type == AttributeType::id_hash || a.type == AttributeType::id_hash_reference) {
        const auto table = section(Section::id_hashes);
        range(table, a.first, 8);
        return {std::bit_cast<std::int32_t>(word(table, a.first)), {}, word(table, a.first + 4)};
    }
    throw std::runtime_error("CXML attribute is not a reference");
}

void ResourceDocument::validate_attribute(const Attribute &a) const {
    switch (a.type) {
    case AttributeType::none: case AttributeType::integer: case AttributeType::real: break;
    case AttributeType::string: (void)string_value(a); break;
    case AttributeType::wide_string: (void)wide_string_value(a); break;
    case AttributeType::hash: (void)hash_value(a); break;
    case AttributeType::integers: (void)integer_values(a); break;
    case AttributeType::reals: (void)real_values(a); break;
    case AttributeType::file: (void)file_value(a); break;
    case AttributeType::id: case AttributeType::id_reference:
    case AttributeType::id_hash: case AttributeType::id_hash_reference: (void)reference_value(a); break;
    default: throw std::runtime_error("Unknown CXML attribute type");
    }
}

void ResourceDocument::validate_tree() const {
    if (elements_.empty() || elements_.front().parent != -1
        || elements_.front().previous != -1 || elements_.front().next != -1)
        throw std::runtime_error("Missing CXML root or invalid root links");
    std::unordered_set<std::uint32_t> linked{0};
    std::vector<std::int32_t> pending{0};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto &parent = element_at(pending[index]);
        std::int32_t previous = -1;
        for (auto child = parent.first_child; child != -1;) {
            const auto &element = element_at(child);
            if (!linked.insert(element.offset).second || element.parent != std::int32_t(parent.offset)
                || element.previous != previous) throw std::runtime_error("Inconsistent CXML child chain");
            pending.push_back(child);
            previous = child;
            child = element.next;
        }
        if (previous != parent.last_child) throw std::runtime_error("Inconsistent CXML last child");
        for (const auto &a : parent.attributes) {
            if (a.type == AttributeType::id || a.type == AttributeType::id_reference
                || a.type == AttributeType::id_hash || a.type == AttributeType::id_hash_reference) {
                const auto ref = reference_value(a);
                if (ref.element != -1) (void)element_at(ref.element);
            }
        }
    }
    if (linked.size() != elements_.size()) throw std::runtime_error("Unreachable CXML elements");
}
} // namespace emucorev::paf
