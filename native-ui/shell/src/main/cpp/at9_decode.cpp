// Theme BGM: decodes a PS Vita ATRAC9 RIFF file (.at9) into a PCM16 WAV so
// the host can play it with a standard player. Uses LibAtrac9 (MIT); no
// emulator code is involved.

#include <jni.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" {
#include <libatrac9.h>
}

namespace {

std::uint32_t u32(const std::vector<std::uint8_t>& d, std::size_t o) {
    return std::uint32_t(d[o]) | std::uint32_t(d[o + 1]) << 8 | std::uint32_t(d[o + 2]) << 16
        | std::uint32_t(d[o + 3]) << 24;
}
std::uint16_t u16(const std::vector<std::uint8_t>& d, std::size_t o) {
    return std::uint16_t(d[o] | d[o + 1] << 8);
}

void put32(std::ofstream& out, std::uint32_t v) {
    const char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
    out.write(b, 4);
}
void put16(std::ofstream& out, std::uint16_t v) {
    const char b[2] = {char(v), char(v >> 8)};
    out.write(b, 2);
}

bool decode(const std::string& in_path, const std::string& out_path) {
    std::ifstream in(in_path, std::ios::binary);
    if (!in) return false;
    std::vector<std::uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return false;
    std::size_t fmt = 0, fmt_size = 0, data = 0, data_size = 0;
    std::uint32_t total_samples = 0, delay = 0;
    for (std::size_t o = 12; o + 8 <= d.size();) {
        const std::uint32_t size = u32(d, o + 4);
        const std::size_t body = o + 8;
        if (body + size > d.size()) break;
        if (!std::memcmp(d.data() + o, "fmt ", 4)) { fmt = body; fmt_size = size; }
        else if (!std::memcmp(d.data() + o, "fact", 4) && size >= 4) {
            total_samples = u32(d, body);
            if (size >= 8) delay = u32(d, body + 4);
        } else if (!std::memcmp(d.data() + o, "data", 4)) { data = body; data_size = size; }
        o = body + size + (size & 1);
    }
    // WAVE_FORMAT_EXTENSIBLE (40 bytes) + version (4) + ATRAC9 config (4).
    if (!fmt || fmt_size < 48 || !data) return false;
    const int channels = u16(d, fmt + 2);
    const std::uint32_t rate = u32(d, fmt + 4);
    unsigned char config[ATRAC9_CONFIG_DATA_SIZE];
    std::memcpy(config, d.data() + fmt + 44, sizeof(config));

    void* handle = Atrac9GetHandle();
    if (!handle) return false;
    std::vector<std::int16_t> pcm;
    bool ok = Atrac9InitDecoder(handle, config) == 0;
    Atrac9CodecInfo info{};
    ok = ok && Atrac9GetCodecInfo(handle, &info) == 0 && info.superframeSize > 0 && info.channels == channels;
    if (ok) {
        std::vector<std::int16_t> frame(std::size_t(info.frameSamples * info.channels));
        const std::uint8_t* p = d.data() + data;
        const std::uint8_t* end = p + data_size;
        while (p + info.superframeSize <= end) {
            const std::uint8_t* sf = p;
            for (int f = 0; f < info.framesInSuperframe; ++f) {
                int used = 0;
                if (Atrac9Decode(handle, sf, frame.data(), &used) != 0 || used <= 0) { ok = false; break; }
                sf += used;
                pcm.insert(pcm.end(), frame.begin(), frame.end());
            }
            if (!ok) break;
            p += info.superframeSize;
        }
    }
    Atrac9ReleaseHandle(handle);
    if (!ok || pcm.empty()) return false;

    // Drop the encoder delay and keep the declared sample count.
    const std::size_t ch = std::size_t(channels);
    std::size_t first = std::size_t(delay) * ch;
    if (first >= pcm.size()) first = 0;
    std::size_t count = pcm.size() - first;
    if (total_samples && std::size_t(total_samples) * ch < count) count = std::size_t(total_samples) * ch;

    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const std::uint32_t bytes = std::uint32_t(count * 2);
    out.write("RIFF", 4); put32(out, 36 + bytes); out.write("WAVE", 4);
    out.write("fmt ", 4); put32(out, 16); put16(out, 1); put16(out, std::uint16_t(channels));
    put32(out, rate); put32(out, rate * ch * 2); put16(out, std::uint16_t(ch * 2)); put16(out, 16);
    out.write("data", 4); put32(out, bytes);
    out.write(reinterpret_cast<const char*>(pcm.data() + first), std::streamsize(bytes));
    return bool(out);
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeDecodeAt9(JNIEnv* env, jclass,
    jstring in_path, jstring out_path) {
    if (!in_path || !out_path) return JNI_FALSE;
    const char* a = env->GetStringUTFChars(in_path, nullptr);
    const char* b = env->GetStringUTFChars(out_path, nullptr);
    bool ok = false;
    if (a && b) ok = decode(a, b);
    if (a) env->ReleaseStringUTFChars(in_path, a);
    if (b) env->ReleaseStringUTFChars(out_path, b);
    return ok ? JNI_TRUE : JNI_FALSE;
}
