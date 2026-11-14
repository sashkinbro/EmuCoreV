/* EmuCoreV: normalize Android's flexible YUV_420_888 planes to packed NV12.
 * The U and V planes may overlap, be planar, and have different row strides.
 * Validate every last addressed byte before writing any output. */
#ifndef ECV_ANDROID_CAMERA_YUV_H
#define ECV_ANDROID_CAMERA_YUV_H

#include <stddef.h>

static int ECV_AndroidPlaneFits(int length, int row_stride, int pixel_stride, int width, int height)
{
    if (length <= 0 || width <= 0 || height <= 0 || pixel_stride <= 0 || row_stride <= 0)
        return 0;
    const size_t row_bytes = (size_t)(width - 1) * pixel_stride + 1;
    return row_bytes <= (size_t)row_stride
        && (size_t)(height - 1) * row_stride + row_bytes <= (size_t)length;
}

static int ECV_PackAndroidYuv420(unsigned char *dst, size_t capacity, int width, int height,
    const unsigned char *y, int yn, int yr, int yp,
    const unsigned char *u, int un, int ur, int up,
    const unsigned char *v, int vn, int vr, int vp)
{
    if (!dst || !y || !u || !v || width <= 0 || height <= 0 || (width & 1) || (height & 1))
        return 0;
    const size_t pixels = (size_t)width * height;
    if (pixels > capacity || pixels / 2 > capacity - pixels
        || !ECV_AndroidPlaneFits(yn, yr, yp, width, height)
        || !ECV_AndroidPlaneFits(un, ur, up, width / 2, height / 2)
        || !ECV_AndroidPlaneFits(vn, vr, vp, width / 2, height / 2))
        return 0;
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
            dst[(size_t)row * width + col] = y[(size_t)row * yr + (size_t)col * yp];
    for (int row = 0; row < height / 2; ++row) {
        for (int col = 0; col < width / 2; ++col) {
            const size_t offset = pixels + (size_t)row * width + col * 2;
            dst[offset] = u[(size_t)row * ur + (size_t)col * up];
            dst[offset + 1] = v[(size_t)row * vr + (size_t)col * vp];
        }
    }
    return 1;
}
#endif
