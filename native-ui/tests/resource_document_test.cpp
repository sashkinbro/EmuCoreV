#include <emucorev/paf/resource_document.h>

#include <bit>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <zlib.h>

using namespace emucorev::paf;
using Bytes = std::vector<std::uint8_t>;

namespace {
void put(Bytes &bytes, std::size_t p, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(p + i) = std::uint8_t(value >> (i * 8));
}
void append(Bytes &bytes, std::uint32_t value) {
    const auto p = bytes.size(); bytes.resize(p + 4); put(bytes, p, value);
}
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
Bytes fixture() {
    Bytes tree, strings{'r','o','o','t',0,'v',0}, ids, id_hashes, hashes, ints, reals;
    // Root followed by one child; root exercises all thirteen attribute types.
    constexpr std::uint32_t child = 28 + 13 * 16;
    for (const auto v : {0u,13u,~0u,~0u,~0u,child,child}) append(tree, v);
    const std::uint32_t payloads[13][2] = {
        {0,0},{0xfffffff9,0},{0x7fc01234,0},{0,4},{1,4},{1,4},
        {1,2},{1,2},{1,2},{0,0},{0,0},{0,0},{0,0}
    };
    for (unsigned type = 0; type < 13; ++type) {
        append(tree, 5); append(tree, type);
        append(tree, payloads[type][0]); append(tree, payloads[type][1]);
    }
    for (const auto v : {0u,0u,0u,~0u,~0u,~0u,~0u}) append(tree, v);
    append(ids, 0); ids.insert(ids.end(), {'r','o','o','t',0});
    append(id_hashes, 0); append(id_hashes, 0x12345678);
    append(hashes, 0xaaaaaaaa); append(hashes, 0xdeadbeef);
    append(ints, 999); append(ints, 0xfffffff9); append(ints, 42);
    append(reals, 0); append(reals, std::bit_cast<std::uint32_t>(1.25f)); append(reals, 0x80000000);
    Bytes wide{0,0,0x16,0x04,0x3f,0x04}, files{0xaa,0xbb,0xcc};
    const Bytes tables[] = {tree, ids, id_hashes, strings, wide, hashes, ints, reals, files};
    Bytes image(80);
    put(image, 0, 0x464f4352); put(image, 4, 0x110);
    for (unsigned i = 0; i < 9; ++i) {
        put(image, 8 + i * 8, std::uint32_t(image.size()));
        put(image, 12 + i * 8, std::uint32_t(tables[i].size()));
        image.insert(image.end(), tables[i].begin(), tables[i].end());
    }
    return image;
}

Bytes compressed_fixture() {
    Bytes tree, strings;
    const auto name = [&](const std::string &text) {
        const auto offset = std::uint32_t(strings.size());
        strings.insert(strings.end(), text.begin(), text.end()); strings.push_back(0); return offset;
    };
    const auto root = name("root"), src = name("src"), compress = name("compress"),
        original = name("origsize"), on = name("on");
    for (const auto v : {root,3u,~0u,~0u,~0u,~0u,~0u}) append(tree, v);
    const Bytes original_bytes{'h','e','l','l','o',0,'p','a','f'};
    Bytes packed(compressBound(original_bytes.size())); uLongf size = packed.size();
    check(compress2(packed.data(), &size, original_bytes.data(), original_bytes.size(), 9) == Z_OK, "Fixture compression");
    packed.resize(size);
    const std::uint32_t attrs[][4] = {{src,8,0,std::uint32_t(size)}, {compress,3,on,2}, {original,1,9,0}};
    for (const auto &a : attrs) for (const auto word : a) append(tree, word);
    const Bytes tables[] = {tree, {}, {}, strings, {}, {}, {}, {}, packed};
    Bytes image(80); put(image, 0, 0x464f4352); put(image, 4, 0x110);
    for (unsigned i = 0; i < 9; ++i) {
        put(image, 8 + i * 8, std::uint32_t(image.size()));
        put(image, 12 + i * 8, std::uint32_t(tables[i].size()));
        image.insert(image.end(), tables[i].begin(), tables[i].end());
    }
    return image;
}
}

int main() {
    try {
        const auto bytes = fixture();
        const ResourceDocument document(bytes);
        const auto &a = document.elements().at(0).attributes;
        check(document.elements().size() == 2, "Element count");
        check(document.string_value(a[3]) == "root", "UTF-8 string");
        check(document.wide_string_value(a[4]) == u"\u0416\u043f", "UTF-16 element index");
        check(document.hash_value(a[5]) == 0xdeadbeef, "Hash element index");
        check(document.integer_values(a[6]) == std::vector<std::int32_t>({-7,42}), "Integer array index");
        const auto floats = document.real_values(a[7]);
        check(floats[0] == 1.25f && std::bit_cast<std::uint32_t>(floats[1]) == 0x80000000, "Float bits");
        check(a[2].first == 0x7fc01234, "NaN payload preservation");
        check(document.file_value(a[8])[0] == 0xbb && document.file_value(a[8]).size() == 2, "File byte offset");
        check(document.decoded_file(document.elements()[0], a[8], 2) == Bytes({0xbb,0xcc}), "Uncompressed payload");
        for (unsigned i = 9; i <= 12; ++i) check(document.reference_value(a[i]).element == 0, "Reference entity");
        check(document.reference_value(a[9]).name == "root", "ID name");
        check(document.reference_value(a[11]).hash == 0x12345678, "ID hash");

        std::size_t rejected = 0;
        const auto rejects = [&](Bytes bad) {
            try { ResourceDocument parsed(std::move(bad)); }
            catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Malformed input accepted");
        };
        // Every byte truncation must fail, including partial table records.
        for (std::size_t size = 0; size < bytes.size(); ++size)
            rejects(Bytes(bytes.begin(), bytes.begin() + size));
        const auto mutate = [&](std::size_t offset, std::uint32_t value) {
            auto bad = bytes; put(bad, offset, value); rejects(std::move(bad));
        };
        mutate(0, 0); mutate(4, 0x111); // Magic/version.
        mutate(8, 0); mutate(16, 80); // Header/table overlap.
        mutate(80 + 4, 0xffffffff); // Attribute count overflow.
        mutate(80 + 12, 0); // Root previous link.
        mutate(80 + 20, 1); // Child link into the middle of a record.
        mutate(80 + 28 + 4, 99); // Unknown attribute type.
        mutate(80 + 28 + 4 * 16 + 12, 3); // UTF-16 lengths are even byte counts.
        for (unsigned type = 3; type <= 12; ++type) {
            if (type >= 9) continue; // 0xffffffff is a legal null reference.
            mutate(80 + 28 + type * 16 + 8, 0xfffffffe);
            if (type != 5) mutate(80 + 28 + type * 16 + 12, 0xffffffff);
        }
        constexpr auto child = 80 + 28 + 13 * 16;
        mutate(child + 8, 123); mutate(child + 16, 28 + 13 * 16); // Parent mismatch/cycle.
        // Null reference remains explicit, with no lookup or fabricated target.
        auto null_image = bytes; put(null_image, 80 + 28 + 9 * 16 + 8, 0xffffffff);
        const ResourceDocument null_document(null_image);
        check(null_document.reference_value(null_document.elements()[0].attributes[9]).element == -1, "Null ID");
        const auto packed = compressed_fixture();
        const ResourceDocument compressed(packed);
        check(compressed.decoded_file(compressed.elements()[0], compressed.elements()[0].attributes[0], 9)
            == Bytes({'h','e','l','l','o',0,'p','a','f'}), "Zlib payload");
        const auto rejects_payload = [&](const Bytes &image, std::size_t limit = 100) {
            try {
                ResourceDocument d(image); (void)d.decoded_file(d.elements()[0], d.elements()[0].attributes[0], limit);
            } catch (const std::runtime_error &) { ++rejected; return; }
            throw std::runtime_error("Invalid compressed payload accepted");
        };
        rejects_payload(packed, 8); // Allocation bound.
        auto bad_packed = packed; bad_packed.back() ^= 0xff; rejects_payload(bad_packed); // Checksum.
        for (const auto size : {0u,8u,10u,0xffffffffu}) {
            bad_packed = packed; put(bad_packed, 80 + 28 + 2 * 16 + 8, size); rejects_payload(bad_packed);
        }
        bad_packed = packed; bad_packed.push_back(0);
        // Table size follows the actual bytes; src includes one trailing byte.
        // Use the header to avoid coupling the test to zlib's output length.
        const auto read_word = [](const Bytes &b, std::size_t p) {
            return std::uint32_t(b[p]) | (std::uint32_t(b[p+1]) << 8)
                | (std::uint32_t(b[p+2]) << 16) | (std::uint32_t(b[p+3]) << 24);
        };
        put(bad_packed, 76, read_word(packed, 76) + 1);
        put(bad_packed, 80 + 28 + 12, read_word(packed, 76) + 1);
        rejects_payload(bad_packed);
        bad_packed = packed; put(bad_packed, 80 + 28 + 12, read_word(packed, 76) - 1);
        rejects_payload(bad_packed); // Truncated zlib stream.
        std::cout << "PASS all attribute types, exact bits, links and " << rejected << " malformed inputs\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
