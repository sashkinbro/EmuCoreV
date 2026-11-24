// EmuCoreV adapter layer: robust PS Vita firmware (PUP) installer. See include/emucorev/firmware_installer.h.
//
// Layout of the work done here (all paths below are inside the Vita root, i.e. on the volume of the final tree so the
// final swap is a rename):
//   .fw_stage/<part>.img   decrypted partition image (one at a time, deleted right after its extraction)
//   .fw_stage/<part>/      extracted partition tree
//   .fw_old/<part>/        previous partition trees while the swap is in progress
//   fw.complete            marker, written last (version + per-partition file counts)
//
// The PUP/SCE parsing mirrors the vendored packages/pup.cpp byte for byte for every input it handled correctly (the host
// tests compare whole installed trees against the vendored installer); the differences are the validation and the I/O.

#include <emucorev/firmware_installer.h>

#include <miniz.h>
#include <openssl/evp.h>
#include <packages/exfat.h>
#include <packages/firmware_info.h>
#include <packages/sce_types.h>
#include <util/log.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <vector>

namespace emucorev::firmware {

const char *status_name(Status status) {
    switch (status) {
    case Status::Ok: return "OK";
    case Status::NotPup: return "NOT_PUP";
    case Status::Truncated: return "TRUNCATED";
    case Status::Corrupt: return "CORRUPT";
    case Status::IoError: return "IO_ERROR";
    case Status::NoSpace: return "NO_SPACE";
    case Status::Cancelled: return "CANCELLED";
    case Status::Internal: return "INTERNAL";
    }
    return "INTERNAL";
}

namespace {

constexpr uint64_t kPupHeaderSize = 0x80;
constexpr uint64_t kPupRecordSize = 0x20;
constexpr uint32_t kMaxRecords = 1024; // real packages hold ~40
constexpr uint64_t kMaxSceHeader = 1u << 20; // real SPKG headers are 0x400
constexpr uint64_t kMaxVersionBytes = 256;
constexpr size_t kChunk = 1u << 20;
constexpr uint64_t kSpaceMargin = 32ull << 20;
// Marker names inside the Vita root.
constexpr const char *kStageName = ".fw_stage";
constexpr const char *kOldName = ".fw_old";
constexpr const char *kCommittedName = ".committed";

struct PartitionSpec {
    const char *name;
    uint32_t spkg_type; // SpkgType value of the package header
};
// Order == the order the vendored installer extracts them (also used for the swap).
constexpr std::array<PartitionSpec, 4> kPartitions{ { { "os0", 1 }, { "pd0", 24 }, { "sa0", 23 }, { "vs0", 10 } } };

struct Failure {
    Status status;
    std::string detail;
    uint64_t required = 0;
};
struct CancelSignal {};

[[noreturn]] void fail(Status status, std::string detail) {
    throw Failure{ status, std::move(detail), 0 };
}

uint32_t rd32(const uint8_t *p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
uint64_t rd64(const uint8_t *p) {
    uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

struct FileCloser {
    void operator()(FILE *f) const {
        if (f)
            std::fclose(f);
    }
};
using FilePtr = std::unique_ptr<FILE, FileCloser>;

#ifdef _WIN32
FilePtr open_file(const fs::path &path, const char *mode_narrow) {
    std::wstring mode;
    for (const char *c = mode_narrow; *c; ++c)
        mode.push_back(static_cast<wchar_t>(*c));
    return FilePtr(_wfopen(path.c_str(), mode.c_str()));
}
#else
FilePtr open_file(const fs::path &path, const char *mode) {
    return FilePtr(std::fopen(path.c_str(), mode));
}
#endif

bool read_at(FILE *f, uint64_t offset, void *buffer, size_t size) {
    if (fseek(f, static_cast<long long>(offset), SEEK_SET) != 0)
        return false;
    return std::fread(buffer, 1, size, f) == size;
}

[[noreturn]] void fail_io(const std::string &what) {
    const int err = errno;
    if (err == ENOSPC)
        throw Failure{ Status::NoSpace, what + ": no space left on device", 0 };
    throw Failure{ Status::IoError, what + ": " + std::strerror(err), 0 };
}

struct Pkg {
    uint64_t rec_offset = 0;
    uint64_t rec_length = 0;
    std::vector<SceSegment> segs;
    uint64_t seg_bytes = 0;
    bool compressed = false;
};

struct PartitionPlan {
    const PartitionSpec *spec = nullptr;
    std::vector<Pkg> pkgs; // record order
    uint64_t seg_bytes = 0;
    bool compressed = false;
};

struct Plan {
    uint64_t file_size = 0;
    std::string version;
    std::vector<PartitionPlan> partitions; // only those present, in kPartitions order
    std::map<uint32_t, uint32_t> package_versions; // SpkgType -> update_version (system-packages/<n>.version)
    uint64_t total_seg_bytes() const {
        uint64_t sum = 0;
        for (const auto &p : partitions)
            sum += p.seg_bytes;
        return sum;
    }
};

Plan parse_pup(FILE *f, uint64_t file_size, KeyStore &keys) {
    Plan plan;
    plan.file_size = file_size;

    uint8_t header[kPupHeaderSize];
    const size_t got = (fseek(f, 0, SEEK_SET) == 0) ? std::fread(header, 1, sizeof(header), f) : 0;
    if (got < 5 || std::memcmp(header, "SCEUF", 5) != 0)
        fail(Status::NotPup, "missing SCEUF magic");
    if (got < kPupHeaderSize)
        fail(Status::Truncated, "file ends inside the PUP header");

    const uint32_t count = rd32(&header[0x18]);
    if (count == 0 || count > kMaxRecords)
        fail(Status::Corrupt, "implausible record count " + std::to_string(count));
    const uint64_t table_end = kPupHeaderSize + static_cast<uint64_t>(count) * kPupRecordSize;
    if (table_end > file_size)
        fail(Status::Truncated, "file ends inside the record table");

    KeyStore &SCE_KEYS = keys;
    std::map<const PartitionSpec *, PartitionPlan> by_partition;

    for (uint32_t i = 0; i < count; i++) {
        uint8_t rec[kPupRecordSize];
        if (!read_at(f, kPupHeaderSize + static_cast<uint64_t>(i) * kPupRecordSize, rec, sizeof(rec)))
            fail(Status::Truncated, "cannot read record " + std::to_string(i));
        const uint64_t type = rd64(&rec[0]);
        const uint64_t offset = rd64(&rec[8]);
        const uint64_t length = rd64(&rec[16]);

        if (offset < table_end)
            fail(Status::Corrupt, "record " + std::to_string(i) + " starts inside the header (offset " + std::to_string(offset) + ")");
        if (offset > file_size || length > file_size - offset)
            fail(Status::Truncated, "record " + std::to_string(i) + " extends past the end of the file");

        if (type == 0x100) { // version.txt
            const size_t n = static_cast<size_t>(std::min<uint64_t>(length, kMaxVersionBytes));
            std::string text(n, '\0');
            if (n && !read_at(f, offset, text.data(), n))
                fail(Status::Truncated, "cannot read version.txt");
            // The vendored installer does std::getline() on the extracted file.
            plan.version = text.substr(0, text.find('\n'));
            continue;
        }
        // Records the vendored installer extracts verbatim and never decrypts.
        if (type == 0x101 || type == 0x200 || type == 0x204 || type == 0x400 || type == 0x401 || type == 0x2005 || type == 0x2006)
            continue;

        // A firmware package: SCE header with the SPKG flags (what pup.cpp:make_filename() recognises).
        uint8_t sce[SceHeader::Size];
        if (length < sizeof(sce) || !read_at(f, offset, sce, sizeof(sce)))
            continue;
        if (rd32(&sce[0]) != SCE_MAGIC || rd32(&sce[4]) != 3 || rd32(&sce[8]) != 0x30040)
            continue;
        const uint64_t header_length = rd64(&sce[16]);
        const uint64_t metadata_offset = rd32(&sce[12]);
        // The header must hold the SPKG header and the encrypted metadata block get_segments() reads.
        if (header_length > kMaxSceHeader || header_length + SpkgHeader::Size > length
            || metadata_offset + 48 + MetadataInfo::Size + MetadataHeader::Size > header_length)
            fail(Status::Corrupt, "record " + std::to_string(i) + ": SCE header length out of range");

        std::vector<uint8_t> hdr(static_cast<size_t>(header_length));
        if (!read_at(f, offset, hdr.data(), hdr.size()))
            fail(Status::Truncated, "cannot read SCE header of record " + std::to_string(i));
        uint8_t spkg_bytes[SpkgHeader::Size];
        if (!read_at(f, offset + header_length, spkg_bytes, sizeof(spkg_bytes)))
            fail(Status::Truncated, "cannot read SPKG header of record " + std::to_string(i));

        const SceHeader sce_hdr(reinterpret_cast<const char *>(hdr.data()));
        if (sce_hdr.sce_type != SceType::SPKG)
            continue;
        const SpkgHeader spkg(reinterpret_cast<const char *>(spkg_bytes));
        const uint8_t t = spkg_bytes[4]; // low byte of the package type, as make_filename() reads it

        const PartitionSpec *spec = nullptr;
        for (const auto &candidate : kPartitions) {
            if (candidate.spkg_type == t)
                spec = &candidate;
        }
        if (!spec)
            continue; // boot/pervasive/... packages are neither decrypted nor installed by this app

        if (spkg.update_version <= UINT32_MAX)
            plan.package_versions.insert_or_assign(t, static_cast<uint32_t>(spkg.update_version));

        std::vector<SceSegment> segs;
        try {
            segs = get_segments(hdr.data(), sce_hdr, SCE_KEYS, spkg.update_version << 16, SelfType::NONE);
        } catch (const std::exception &e) {
            fail(Status::Corrupt, std::string("record ") + std::to_string(i) + ": " + e.what());
        }
        if (segs.empty())
            fail(Status::Corrupt, "record " + std::to_string(i) + " has no data segment");

        Pkg pkg;
        pkg.rec_offset = offset;
        pkg.rec_length = length;
        for (const auto &seg : segs) {
            if (seg.key.size() != 16 || seg.iv.size() != 16)
                fail(Status::Corrupt, "record " + std::to_string(i) + ": bad segment key");
            if (seg.offset > length || seg.size > length - seg.offset)
                fail(Status::Truncated, "record " + std::to_string(i) + ": segment extends past the record");
            pkg.seg_bytes += seg.size;
            pkg.compressed = pkg.compressed || seg.compressed;
        }
        pkg.segs = std::move(segs);

        auto &part = by_partition[spec];
        part.spec = spec;
        part.seg_bytes += pkg.seg_bytes;
        part.compressed = part.compressed || pkg.compressed;
        part.pkgs.push_back(std::move(pkg));
    }

    for (const auto &spec : kPartitions) {
        auto it = by_partition.find(&spec);
        if (it != by_partition.end())
            plan.partitions.push_back(std::move(it->second));
    }
    if (plan.partitions.empty())
        fail(Status::Corrupt, "the package contains no os0/pd0/vs0/sa0 partition");
    return plan;
}

// Progress / cancel plumbing shared by the phases.
class Reporter {
public:
    Reporter(const Options &options, uint32_t partition_count)
        : options_(options)
        , partition_count_(partition_count) {}

    void check_cancel() const {
        if (options_.cancelled && options_.cancelled())
            throw CancelSignal{};
    }

    void set_partition(uint32_t index, const std::string &name) {
        index_ = index;
        name_ = name;
    }
    void add_files(uint64_t n) { files_ += n; }
    uint64_t files() const { return files_; }

    // Overall percent in [0,100]; the callback is throttled to ~0.5% steps / phase changes / file counts.
    void emit(Phase phase, float percent, bool force = false) {
        percent = std::max(percent_, std::min(percent, 100.f)); // monotonic
        percent_ = percent;
        if (!options_.progress)
            return;
        const bool changed = force || phase != last_phase_ || index_ != last_index_ || percent >= last_emitted_ + 0.5f || files_ >= last_files_ + 64;
        if (!changed)
            return;
        last_phase_ = phase;
        last_index_ = index_;
        last_emitted_ = percent;
        last_files_ = files_;
        Progress p;
        p.percent = percent;
        p.phase = phase;
        p.partition = (phase == Phase::Decrypt || phase == Phase::Extract) ? name_ : std::string();
        p.partition_index = index_;
        p.partition_count = partition_count_;
        p.files = files_;
        options_.progress(p);
    }

private:
    const Options &options_;
    uint32_t partition_count_;
    uint32_t index_ = 0;
    std::string name_;
    uint64_t files_ = 0;
    float percent_ = 0.f;
    float last_emitted_ = -1.f;
    uint64_t last_files_ = 0;
    uint32_t last_index_ = 0;
    Phase last_phase_ = Phase::Validate;
};

struct EvpCtxDeleter {
    void operator()(EVP_CIPHER_CTX *c) const { EVP_CIPHER_CTX_free(c); }
};
struct EvpCipherDeleter {
    void operator()(EVP_CIPHER *c) const { EVP_CIPHER_free(c); }
};

void write_all(FILE *out, const uint8_t *data, size_t size, const std::string &what) {
    if (size && std::fwrite(data, 1, size, out) != size)
        fail_io("write failed for " + what);
}

// Decrypts one segment (AES-128-CTR, optionally deflated) from the PUP straight into `out`.
// Memory: one chunk (1 MiB) + the 64 KiB inflate window, independent of the package size.
void decrypt_segment(FILE *pup, const Pkg &pkg, const SceSegment &seg, EVP_CIPHER *cipher, FILE *out, const std::string &out_name,
    Reporter &reporter, const std::function<void(uint64_t)> &advance) {
    std::unique_ptr<EVP_CIPHER_CTX, EvpCtxDeleter> ctx(EVP_CIPHER_CTX_new());
    if (!ctx)
        fail(Status::Internal, "EVP_CIPHER_CTX_new failed");
    if (EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, reinterpret_cast<const unsigned char *>(seg.key.data()),
            reinterpret_cast<const unsigned char *>(seg.iv.data()))
        != 1)
        fail(Status::Internal, "EVP_DecryptInit_ex failed");
    EVP_CIPHER_CTX_set_padding(ctx.get(), 0);

    mz_stream zs{};
    bool inflating = false;
    bool stream_end = false;
    if (seg.compressed) {
        if (mz_inflateInit(&zs) != MZ_OK)
            fail(Status::Internal, "inflateInit failed");
        inflating = true;
    }
    struct InflateEnd {
        mz_stream *s;
        bool *active;
        ~InflateEnd() {
            if (*active)
                mz_inflateEnd(s);
        }
    } inflate_guard{ &zs, &inflating };

    std::vector<uint8_t> in(kChunk), dec(kChunk), inflated(seg.compressed ? 64 * 1024 : 0);
    uint64_t remaining = seg.size;
    uint64_t position = pkg.rec_offset + seg.offset;
    if (fseek(pup, static_cast<long long>(position), SEEK_SET) != 0)
        fail(Status::IoError, "seek failed in the firmware package");

    while (remaining) {
        reporter.check_cancel();
        const size_t n = static_cast<size_t>(std::min<uint64_t>(remaining, in.size()));
        if (std::fread(in.data(), 1, n, pup) != n)
            fail(Status::Truncated, "firmware package ended while reading a data segment");
        int dec_len = 0;
        if (EVP_DecryptUpdate(ctx.get(), dec.data(), &dec_len, in.data(), static_cast<int>(n)) != 1 || dec_len < 0)
            fail(Status::Internal, "EVP_DecryptUpdate failed");

        if (!seg.compressed) {
            write_all(out, dec.data(), static_cast<size_t>(dec_len), out_name);
        } else if (!stream_end) {
            zs.next_in = dec.data();
            zs.avail_in = static_cast<unsigned int>(dec_len);
            for (;;) {
                zs.next_out = inflated.data();
                zs.avail_out = static_cast<unsigned int>(inflated.size());
                const int ret = mz_inflate(&zs, MZ_NO_FLUSH);
                const size_t produced = inflated.size() - zs.avail_out;
                write_all(out, inflated.data(), produced, out_name);
                if (ret == MZ_STREAM_END) {
                    stream_end = true;
                    break;
                }
                if (ret != MZ_OK && ret != MZ_BUF_ERROR)
                    fail(Status::Corrupt, "deflate stream is damaged (" + std::to_string(ret) + ")");
                if (zs.avail_in == 0 && zs.avail_out != 0)
                    break; // needs more input
                if (ret == MZ_BUF_ERROR && produced == 0)
                    break;
            }
        }
        remaining -= n;
        advance(n);
    }
    if (seg.compressed && !stream_end)
        fail(Status::Corrupt, "deflate stream ended early");
}

uint64_t count_files(const fs::path &dir) {
    uint64_t n = 0;
    for (fs::recursive_directory_iterator it(dir), end; it != end; ++it) {
        if (fs::is_regular_file(it->path()))
            n++;
    }
    return n;
}

std::string read_text(const fs::path &path, bool &exists) {
    exists = false;
    if (!fs::exists(path))
        return {};
    fs::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    exists = true;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_text_atomic(const fs::path &path, const std::string &text) {
    const fs::path tmp = path.string() + ".tmp";
    {
        fs::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            fail_io("cannot create " + fs_utils::path_to_utf8(tmp));
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
            fail_io("cannot write " + fs_utils::path_to_utf8(tmp));
    }
    boost::system::error_code ec;
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw Failure{ Status::IoError, "cannot replace " + fs_utils::path_to_utf8(path), 0 };
    }
}

struct Marker {
    std::string version;
    std::map<std::string, uint64_t> files;
};

constexpr const char *kMarkerHeader = "emucorev-firmware-marker 1";

std::optional<Marker> parse_marker(const std::string &text) {
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || line.rfind(kMarkerHeader, 0) != 0)
        return std::nullopt;
    Marker m;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "version")
            m.version = value;
        else if (key.size() > 6 && key.compare(key.size() - 6, 6, ".files") == 0) {
            try {
                m.files[key.substr(0, key.size() - 6)] = std::stoull(value);
            } catch (...) {
            }
        }
    }
    return m;
}

std::string format_marker(const Marker &m) {
    std::ostringstream out;
    out << kMarkerHeader << '\n' << "version=" << m.version << '\n';
    for (const auto &[name, count] : m.files)
        out << name << ".files=" << count << '\n';
    return out.str();
}

// A directory rename can transiently fail while another process (indexer, antivirus, file manager) holds a handle in it.
boost::system::error_code rename_retry_ec(const fs::path &from, const fs::path &to) {
    boost::system::error_code ec;
    for (int attempt = 0; attempt < 6; attempt++) {
        fs::rename(from, to, ec);
        if (!ec)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return ec;
}

void rename_retry(const fs::path &from, const fs::path &to) {
    const auto ec = rename_retry_ec(from, to);
    if (ec)
        throw boost::system::system_error(ec, "rename " + fs_utils::path_to_utf8(from) + " -> " + fs_utils::path_to_utf8(to));
}

bool is_mapped_no_space(const boost::system::error_code &ec) {
    return ec == boost::system::errc::no_space_on_device;
}

struct StageGuard {
    fs::path root;
    ~StageGuard() {
        boost::system::error_code ec;
        fs::remove_all(root / kStageName, ec);
        fs::remove_all(root / "PUP_DEC", ec); // leaked by pre-staging installers
    }
};

} // namespace

void recover_interrupted(const fs::path &root) {
    boost::system::error_code ec;
    const fs::path old_dir = root / kOldName;
    if (fs::exists(old_dir, ec)) {
        if (fs::exists(old_dir / kCommittedName, ec)) {
            // The swap finished; only the cleanup was interrupted.
            fs::remove_all(old_dir, ec);
        } else {
            // The swap was interrupted: put every replaced partition back.
            for (fs::directory_iterator it(old_dir, ec), end; !ec && it != end; ++it) {
                if (!fs::is_directory(it->path()))
                    continue;
                const fs::path target = root / it->path().filename();
                LOG_WARN("Firmware install was interrupted; restoring {}", it->path().filename().string());
                fs::remove_all(target, ec);
                fs::rename(it->path(), target, ec);
            }
            fs::remove_all(old_dir, ec);
        }
    }
    fs::remove_all(root / kStageName, ec);
    fs::remove_all(root / "PUP_DEC", ec);
}

Result install(const fs::path &root, const fs::path &pup_path, const Options &options) {
    Result result;
    std::unique_ptr<StageGuard> guard;
    try {
        fs::create_directories(root);
        recover_interrupted(root);
        guard = std::make_unique<StageGuard>(StageGuard{ root });

        FilePtr pup = open_file(pup_path, "rb");
        if (!pup)
            fail_io("cannot open " + fs_utils::path_to_utf8(pup_path));
        boost::system::error_code size_ec;
        const uint64_t file_size = fs::file_size(pup_path, size_ec);
        if (size_ec)
            fail(Status::IoError, "cannot stat the firmware package");

        KeyStore keys;
        register_keys(keys, 0);
        Plan plan = parse_pup(pup.get(), file_size, keys);
        LOG_INFO("Firmware package {}: version '{}', {} partition(s)", fs_utils::path_to_utf8(pup_path), plan.version, plan.partitions.size());

        const uint32_t part_count = static_cast<uint32_t>(plan.partitions.size());
        Reporter reporter(options, part_count);
        reporter.emit(Phase::Validate, 1.f, true);

        // Free space: staged trees + the one image being extracted + the previous trees kept until the swap.
        uint64_t sum_est = 0, max_est = 0;
        for (const auto &p : plan.partitions) {
            const uint64_t est = p.seg_bytes * (p.compressed ? 2 : 1);
            sum_est += est;
            max_est = std::max(max_est, est);
        }
        const uint64_t required = 2 * sum_est + max_est + kSpaceMargin;
        uint64_t available = 0;
        if (options.available_bytes) {
            available = options.available_bytes();
        } else {
            boost::system::error_code ec;
            available = fs::space(root, ec).available;
            if (ec)
                available = UINT64_MAX; // cannot tell; rely on ENOSPC handling
        }
        if (available < required) {
            Failure f{ Status::NoSpace, "not enough free space", required };
            throw f;
        }

        const fs::path stage = root / kStageName;
        fs::create_directories(stage);

        const uint64_t decrypt_total = std::max<uint64_t>(plan.total_seg_bytes(), 1);
        uint64_t decrypt_done = 0;
        std::unique_ptr<EVP_CIPHER, EvpCipherDeleter> cipher(EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr));
        if (!cipher)
            fail(Status::Internal, "AES-128-CTR is not available");

        // Phase 1: decrypt every partition package into its image. Phase weights: validate 0-2, decrypt 2-40, extract 40-95.
        std::map<std::string, uint64_t> image_sizes;
        uint64_t image_total = 0;
        for (size_t pi = 0; pi < plan.partitions.size(); pi++) {
            const auto &part = plan.partitions[pi];
            reporter.set_partition(static_cast<uint32_t>(pi + 1), part.spec->name);
            const std::string image_name = std::string(part.spec->name) + ".img";
            FilePtr out = open_file(stage / image_name, "wb");
            if (!out)
                fail_io("cannot create " + image_name);
            for (const auto &pkg : part.pkgs) {
                for (const auto &seg : pkg.segs) {
                    decrypt_segment(pup.get(), pkg, seg, cipher.get(), out.get(), image_name, reporter, [&](uint64_t n) {
                        decrypt_done += n;
                        reporter.emit(Phase::Decrypt, 2.f + 38.f * static_cast<float>(decrypt_done) / static_cast<float>(decrypt_total));
                    });
                }
            }
            FILE *raw = out.release();
            if (std::fclose(raw) != 0)
                fail_io("close failed for " + image_name);
            boost::system::error_code ec;
            const uint64_t size = fs::file_size(stage / image_name, ec);
            image_sizes[part.spec->name] = ec ? 0 : size;
            image_total += ec ? 0 : size;
        }
        pup.reset();

        // Phase 2: extract each image into <stage>/<part>/ and drop the image.
        Marker marker;
        std::map<std::string, uint64_t> counts;
        uint64_t extracted_bytes = 0;
        const uint64_t extract_total = std::max<uint64_t>(image_total, 1);
        for (size_t pi = 0; pi < plan.partitions.size(); pi++) {
            const auto &part = plan.partitions[pi];
            const std::string name = part.spec->name;
            reporter.set_partition(static_cast<uint32_t>(pi + 1), name);
            reporter.check_cancel();
            const std::string image_name = name + ".img";
            if (image_sizes[name] > 0) {
                const auto tick = [&](uint64_t bytes) {
                    reporter.check_cancel();
                    if (bytes == 0)
                        reporter.add_files(1);
                    extracted_bytes += bytes;
                    reporter.emit(Phase::Extract, 40.f + 55.f * std::min(1.f, static_cast<float>(extracted_bytes) / static_cast<float>(extract_total)));
                };
                if (name == "pd0")
                    exfat::extract_exfat(stage, image_name, stage, tick);
                else
                    extract_fat(stage, image_name, stage, tick);
            }
            boost::system::error_code ec;
            fs::remove(stage / image_name, ec);

            if (!fs::is_directory(stage / name))
                fail(Status::Corrupt, name + " image holds no files");
            const uint64_t files = count_files(stage / name);
            if (files == 0)
                fail(Status::Corrupt, name + " image holds no files");
            if (name == "vs0" && !fs::is_regular_file(stage / "vs0" / "vsh" / "shell" / "shell.self"))
                fail(Status::Corrupt, "vs0 is missing vsh/shell/shell.self");
            counts[name] = files;
        }

        // Component versions (VSH asks for them at startup): staged, swapped in with the partitions.
        std::map<uint32_t, uint32_t> versions;
        for (const auto &[type, version] : plan.package_versions)
            versions[type] = version;
        // Only meaningful (and, as before, only written) when the package carries a firmware version.
        const bool write_versions = !plan.version.empty();

        reporter.check_cancel();
        reporter.set_partition(0, "");
        reporter.emit(Phase::Commit, 95.f, true);
        reporter.check_cancel(); // last point of no return: after this the swap runs to completion

        // Marker contents: keep what earlier packages (e.g. the base firmware before the fonts package) installed.
        bool marker_existed = false;
        const std::string old_marker_text = read_text(root / kMarkerName, marker_existed);
        if (auto previous = marker_existed ? parse_marker(old_marker_text) : std::nullopt)
            marker = *previous;
        const bool has_base_parts = counts.count("vs0") || counts.count("os0");
        if (marker.version.empty() || has_base_parts)
            marker.version = plan.version;
        for (const auto &[name, files] : counts)
            marker.files[name] = files;

        // Commit. Everything above only touched .fw_stage; the previous trees stay until the very last step.
        const fs::path old_dir = root / kOldName;
        fs::remove_all(old_dir);
        fs::create_directories(old_dir);

        struct Swapped {
            std::string name;
            bool had_old;
            bool installed;
        };
        std::vector<Swapped> swapped;
        struct VersionBackup {
            fs::path path;
            bool existed;
            std::string text;
        };
        std::vector<VersionBackup> version_backups;
        const auto rollback = [&]() {
            boost::system::error_code ec;
            for (auto it = swapped.rbegin(); it != swapped.rend(); ++it) {
                if (it->installed)
                    fs::remove_all(root / it->name, ec);
                if (it->had_old)
                    ec = rename_retry_ec(old_dir / it->name, root / it->name);
            }
            for (const auto &b : version_backups) {
                if (b.existed) {
                    fs::ofstream out(b.path, std::ios::binary | std::ios::trunc);
                    out.write(b.text.data(), static_cast<std::streamsize>(b.text.size()));
                } else
                    fs::remove(b.path, ec);
            }
            if (marker_existed) {
                fs::ofstream out(root / kMarkerName, std::ios::binary | std::ios::trunc);
                out.write(old_marker_text.data(), static_cast<std::streamsize>(old_marker_text.size()));
            } else
                fs::remove(root / kMarkerName, ec);
            fs::remove_all(old_dir, ec);
        };

        try {
            for (const auto &part : plan.partitions) {
                const std::string name = part.spec->name;
                Swapped s{ name, false, false };
                if (fs::exists(root / name)) {
                    rename_retry(root / name, old_dir / name);
                    s.had_old = true;
                }
                swapped.push_back(s);
                rename_retry(stage / name, root / name);
                swapped.back().installed = true;
            }
            if (write_versions) {
                for (const auto &[type, version] : versions) {
                    const auto path = ::firmware::package_version_path(root, type);
                    fs::create_directories(path.parent_path());
                    bool existed = false;
                    version_backups.push_back({ path, false, read_text(path, existed) });
                    version_backups.back().existed = existed;
                    std::ostringstream text;
                    text << std::hex << version << '\n';
                    write_text_atomic(path, text.str());
                }
            }
            write_text_atomic(root / kMarkerName, format_marker(marker));
            {
                fs::ofstream committed(old_dir / kCommittedName);
                committed << "1\n";
                if (!committed)
                    fail_io("cannot finish the install");
            }
        } catch (...) {
            rollback();
            throw;
        }
        {
            boost::system::error_code ec;
            fs::remove_all(old_dir, ec);
        }
        // The install is complete; a throwing progress callback must not turn it into a reported failure.
        try {
            reporter.emit(Phase::Commit, 100.f, true);
        } catch (...) {
        }

        result.status = Status::Ok;
        result.version = plan.version;
        result.files = counts;
        return result;
    } catch (const CancelSignal &) {
        result.status = Status::Cancelled;
        result.detail = "cancelled";
    } catch (const Failure &f) {
        result.status = f.status;
        result.detail = f.detail;
        result.required_bytes = f.required;
    } catch (const std::system_error &e) {
        result.status = (e.code() == std::errc::no_space_on_device) ? Status::NoSpace : Status::IoError;
        result.detail = e.what();
    } catch (const boost::system::system_error &e) {
        result.status = is_mapped_no_space(e.code()) ? Status::NoSpace : Status::IoError;
        result.detail = e.what();
    } catch (const std::bad_alloc &) {
        result.status = Status::Internal;
        result.detail = "out of memory";
    } catch (const std::runtime_error &e) {
        // The vendored extractors report damaged partition images (short reads, corrupt metadata) as runtime_error.
        result.status = Status::Corrupt;
        result.detail = e.what();
    } catch (const std::exception &e) {
        result.status = Status::Internal;
        result.detail = e.what();
    } catch (...) {
        result.status = Status::Internal;
        result.detail = "unknown exception";
    }
    LOG_ERROR("Firmware install failed: {} ({})", status_name(result.status), result.detail);
    return result;
}

} // namespace emucorev::firmware
