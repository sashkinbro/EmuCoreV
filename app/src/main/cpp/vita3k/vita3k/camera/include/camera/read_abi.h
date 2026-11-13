// The original 0x38-byte camera read ABI predates exposure/raw metadata.
#ifndef ECV_CAMERA_READ_ABI_H
#define ECV_CAMERA_READ_ABI_H
static int ecv_camera_read_buffers(const unsigned char *read, unsigned int size, unsigned int out[6])
{
    if (!read || !out || (size != 56 && size != 72)) return 0;
    const unsigned int offset = size == 56 ? 32 : 48;
    for (unsigned int i = 0; i < 6; ++i) {
        const unsigned char *p = read + offset + i * 4;
        out[i] = (unsigned int)p[0] | ((unsigned int)p[1] << 8)
            | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
    }
    return 1;
}
#endif
