#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace emucorev::paf {

enum class AttributeType : std::uint32_t {
    none, integer, real, string, wide_string, hash, integers,
    reals, file, id, id_reference, id_hash, id_hash_reference
};

enum class Section : std::size_t {
    tree, ids, id_hashes, strings, wide_strings, hashes, integers, reals, files
};

struct Attribute {
    std::string name;
    AttributeType type;
    // Preserve all original bits. Floats and unknown/reserved words are never
    // converted through a text representation in the runtime document.
    // first is a table element index for hashes/numeric arrays/UTF-16,
    // but a byte offset for UTF-8, files and ID records.
    // UTF-16 second is a byte length, not a count of code units.
    std::uint32_t first;
    std::uint32_t second;
};

struct Element {
    std::uint32_t offset;
    std::string name;
    std::int32_t parent, previous, next, first_child, last_child;
    std::vector<Attribute> attributes;
};

struct Reference {
    std::int32_t element;
    std::string name;
    std::uint32_t hash;
};

class ResourceDocument {
public:
    explicit ResourceDocument(std::vector<std::uint8_t> bytes);

    const std::string &magic() const { return magic_; }
    std::uint32_t version() const { return version_; }
    const std::vector<Element> &elements() const { return elements_; }
    std::span<const std::uint8_t> section(Section kind) const;
    const Element &element_at(std::int32_t offset) const;

    std::string string_value(const Attribute &attribute) const;
    std::u16string wide_string_value(const Attribute &attribute) const;
    std::vector<std::int32_t> integer_values(const Attribute &attribute) const;
    std::vector<float> real_values(const Attribute &attribute) const;
    std::span<const std::uint8_t> file_value(const Attribute &attribute) const;
    // Decode the original per-element compression metadata. The caller bounds
    // allocations explicitly; no filename or filesystem access is inferred.
    std::vector<std::uint8_t> decoded_file(const Element &owner, const Attribute &attribute,
        std::size_t maximum_bytes) const;
    std::uint32_t hash_value(const Attribute &attribute) const;
    Reference reference_value(const Attribute &attribute) const;

private:
    struct Table { std::uint32_t offset, size; };
    std::vector<std::uint8_t> bytes_;
    std::string magic_;
    std::uint32_t version_;
    Table tables_[9]{};
    std::vector<Element> elements_;

    void validate_attribute(const Attribute &attribute) const;
    void validate_tree() const;
};

} // namespace emucorev::paf
