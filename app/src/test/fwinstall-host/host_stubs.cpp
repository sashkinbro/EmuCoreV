// Link stubs for the SDL3 file API that the core's fs_utils.cpp uses (the installer links no SDL): plain stdio equivalents.
#include <cstdio>

extern "C" {
struct SDL_IOStream;
SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode) { return reinterpret_cast<SDL_IOStream *>(std::fopen(file, mode)); }
long long SDL_GetIOSize(SDL_IOStream *s) {
    FILE *f = reinterpret_cast<FILE *>(s);
    const long pos = std::ftell(f);
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, pos, SEEK_SET);
    return size;
}
size_t SDL_ReadIO(SDL_IOStream *s, void *ptr, size_t size) { return std::fread(ptr, 1, size, reinterpret_cast<FILE *>(s)); }
bool SDL_CloseIO(SDL_IOStream *s) { return std::fclose(reinterpret_cast<FILE *>(s)) == 0; }
}
