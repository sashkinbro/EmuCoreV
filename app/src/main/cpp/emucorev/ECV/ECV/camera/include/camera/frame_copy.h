// EmuCoreV camera guest layout. SceCameraInfo.pitch is extra bytes per luma/packed row.
// SDL YV12 is Y,V,U; guest planar formats are Y,U,V. Padding stays untouched.
#ifndef ECV_CAMERA_FRAME_COPY_H
#define ECV_CAMERA_FRAME_COPY_H
#include <stddef.h>

static size_t ecv_camera_plane_size(int stride, int bytes, int rows)
{
    return (size_t)(rows - 1) * stride + bytes;
}

static int ecv_camera_copy(int format, int width, int height, int padding, int reverse,
    const unsigned char *src, int pitch, size_t source_size,
    unsigned char *y, size_t yn, unsigned char *u, size_t un, unsigned char *v, size_t vn)
{
    if (!src || !y || width <= 0 || height <= 0 || padding < 0 || width > 640 || height > 480
        || format < 1 || format > 5 || reverse < 0 || reverse > 3 || (width & 1) || (height & 1))
        return 0;
    const int planar = format == 1 || format == 3;
    const int bytes = (format == 4 || format == 5) ? 4 : (format == 2 ? 2 : 1);
    const int stride = width * bytes + padding;
    const int source_row = width * ((format == 1 || format == 2) ? 2 : bytes);
    const int chroma_stride = (width + padding + 1) / 2;
    const int source_chroma_stride = (pitch + 1) / 2;
    const int chroma_height = format == 3 ? height / 2 : height;
    if (pitch < source_row || yn < ecv_camera_plane_size(stride, width * bytes, height))
        return 0;
    if (planar && (!u || !v || un < ecv_camera_plane_size(chroma_stride, width / 2, chroma_height)
        || vn < ecv_camera_plane_size(chroma_stride, width / 2, chroma_height)))
        return 0;
    size_t needed = ecv_camera_plane_size(pitch, source_row, height);
    if (format == 3)
        needed = (size_t)pitch * height + (size_t)source_chroma_stride * (height / 2)
            + ecv_camera_plane_size(source_chroma_stride, width / 2, height / 2);
    if (source_size < needed)
        return 0;
    const int mirror = reverse & 1, flip = reverse & 2;
    for (int row = 0; row < height; ++row) {
        const unsigned char *s = src + (size_t)(flip ? height - row - 1 : row) * pitch;
        unsigned char *d = y + (size_t)row * stride;
        for (int x = 0; x < width; ++x) {
            const int sx = mirror ? width - x - 1 : x;
            if (format == 1) {
                d[x] = s[sx * 2];
            } else if (format == 2) {
                d[x * 2] = s[sx * 2];
                const int pair = mirror ? width / 2 - x / 2 - 1 : x / 2;
                d[x * 2 + 1] = s[pair * 4 + ((x & 1) ? 3 : 1)];
            } else {
                for (int b = 0; b < bytes; ++b)
                    d[x * bytes + b] = s[sx * bytes + b];
            }
        }
    }
    if (planar) {
        for (int row = 0; row < chroma_height; ++row) {
            const int sy = flip ? chroma_height - row - 1 : row;
            for (int x = 0; x < width / 2; ++x) {
                const int sx = mirror ? width / 2 - x - 1 : x;
                unsigned char ub, vb;
                if (format == 3) {
                    const size_t base = (size_t)pitch * height;
                    vb = src[base + (size_t)sy * source_chroma_stride + sx];
                    ub = src[base + (size_t)source_chroma_stride * chroma_height + (size_t)sy * source_chroma_stride + sx];
                } else {
                    ub = src[(size_t)sy * pitch + sx * 4 + 1];
                    vb = src[(size_t)sy * pitch + sx * 4 + 3];
                }
                u[(size_t)row * chroma_stride + x] = ub;
                v[(size_t)row * chroma_stride + x] = vb;
            }
        }
    }
    return 1;
}
#endif
