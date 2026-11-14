#include "OpenSSLCryptoOperations.h"

#include <openssl/evp.h>
#include <cstring>

OpenSSLCryptoOperations::OpenSSLCryptoOperations() {
    cipher_ctx = EVP_CIPHER_CTX_new();
    md_ctx = EVP_MD_CTX_new();

    mac_cmac = EVP_MAC_fetch(nullptr, "CMAC", nullptr);
    mac_hmac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);

    cmac_ctx = EVP_MAC_CTX_new(mac_cmac);
    hmac_ctx = EVP_MAC_CTX_new(mac_hmac);

    cipher_aes_cbc = EVP_CIPHER_fetch(nullptr, "AES-128-CBC", nullptr);
    cipher_aes_ctr = EVP_CIPHER_fetch(nullptr, "AES-128-CTR", nullptr);
    cipher_aes_ecb = EVP_CIPHER_fetch(nullptr, "AES-128-ECB", nullptr);

    md_sha1 = EVP_MD_fetch(nullptr, "SHA1", nullptr);
    md_sha256 = EVP_MD_fetch(nullptr, "SHA256", nullptr);

    hmac_sha1_param = new OSSL_PARAM[2];
    hmac_sha1_param[0] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char *>("SHA1"), 0);
    hmac_sha1_param[1] = OSSL_PARAM_construct_end();
    hmac_sha256_param = new OSSL_PARAM[2];
    hmac_sha256_param[0] = OSSL_PARAM_construct_utf8_string("digest", const_cast<char *>("SHA256"), 0);
    hmac_sha256_param[1] = OSSL_PARAM_construct_end();
}

OpenSSLCryptoOperations::~OpenSSLCryptoOperations() {
    delete[] hmac_sha1_param;
    delete[] hmac_sha256_param;

    EVP_MD_free(md_sha1);
    EVP_MD_free(md_sha256);

    EVP_CIPHER_free(cipher_aes_cbc);
    EVP_CIPHER_free(cipher_aes_ctr);
    EVP_CIPHER_free(cipher_aes_ecb);

    EVP_MAC_CTX_free(cmac_ctx);
    EVP_MAC_CTX_free(hmac_ctx);

    EVP_MAC_free(mac_cmac);
    EVP_MAC_free(mac_hmac);

    EVP_CIPHER_CTX_free(cipher_ctx);
    EVP_MD_CTX_free(md_ctx);
}

int OpenSSLCryptoOperations::aes_cbc_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const {
    if (size == 0)
        return 0;

    int result = cipher_encrypt(cipher_aes_cbc, src, dst, size, key, key_size, iv);
    if (result != 0)
        return result;

    // the new IV is the last encoded block
    std::memcpy(iv, dst + size - 0x10, 0x10);

    return 0;
}

int OpenSSLCryptoOperations::aes_cbc_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const {
    if (size == 0)
        return 0;

    // the new IV is the last encoded block
    // copy it here in case src and dst are aliased
    unsigned char new_iv[0x10];
    std::memcpy(new_iv, src + size - 0x10, 0x10);

    int result = cipher_decrypt(cipher_aes_cbc, src, dst, size, key, key_size, iv);
    if (result != 0)
        return result;

    std::memcpy(iv, new_iv, 0x10);

    return 0;
}

int OpenSSLCryptoOperations::aes_ctr_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) {
    return aes_ctr(src, dst, size, key, key_size, iv);
}

int OpenSSLCryptoOperations::aes_ctr_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) {
    return aes_ctr(src, dst, size, key, key_size, iv);
}

int OpenSSLCryptoOperations::aes_ecb_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    return cipher_encrypt(cipher_aes_ecb, src, dst, size, key, key_size, nullptr);
}

int OpenSSLCryptoOperations::aes_ecb_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    return cipher_decrypt(cipher_aes_ecb, src, dst, size, key, key_size, nullptr);
}

int OpenSSLCryptoOperations::aes_cmac(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    if (key_size != 128)
        return -1;

    OSSL_PARAM params[2] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char *>("AES128"), 0),
        OSSL_PARAM_construct_end()
    };

    if (EVP_MAC_init(cmac_ctx, key, key_size, params) != 1)
        return -1;

    if (EVP_MAC_update(cmac_ctx, src, size) != 1)
        return -1;

    size_t dstlen = 0x10;
    if (EVP_MAC_final(cmac_ctx, dst, &dstlen, dstlen) != 1)
        return -1;

    return 0;
}

int OpenSSLCryptoOperations::sha1(const unsigned char *src, unsigned char *dst, int size) const {
    return sha(md_sha1, src, dst, size);
}

int OpenSSLCryptoOperations::sha256(const unsigned char *src, unsigned char *dst, int size) const {
    return sha(md_sha256, src, dst, size);
}

int OpenSSLCryptoOperations::hmac_sha1(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    return hmac_sha(hmac_sha1_param, 20, src, dst, size, key, key_size);
}

int OpenSSLCryptoOperations::hmac_sha256(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    return hmac_sha(hmac_sha256_param, 32, src, dst, size, key, key_size);
}

int OpenSSLCryptoOperations::aes_ctr(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const {
    int result = cipher_encrypt(cipher_aes_ctr, src, dst, size, key, key_size, iv);
    if (result != 0)
        return result;

    // 128 bit big-endian addition
    uint64_t to_add = size / 0x10;
    for (int i = 15; i >= 0; i--) {
        to_add += iv[i];
        iv[i] = static_cast<uint8_t>(to_add);
        to_add >>= 8;
    }

    return 0;
}

int OpenSSLCryptoOperations::cipher_encrypt(const EVP_CIPHER *cipher, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const {
    if (key_size != 128)
        return -1;

    if (EVP_EncryptInit_ex(cipher_ctx, cipher, nullptr, key, iv) != 1)
        return -1;
    EVP_CIPHER_CTX_set_padding(cipher_ctx, 0);

    int len;
    if (EVP_EncryptUpdate(cipher_ctx, dst, &len, src, size) != 1)
        return -1;

    if (EVP_EncryptFinal_ex(cipher_ctx, dst + len, &len) != 1)
        return -1;

    return 0;
}

int OpenSSLCryptoOperations::cipher_decrypt(const EVP_CIPHER *cipher, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const {
    if (key_size != 128)
        return -1;

    if (EVP_DecryptInit_ex(cipher_ctx, cipher, nullptr, key, iv) != 1)
        return -1;
    EVP_CIPHER_CTX_set_padding(cipher_ctx, 0);

    int len;
    if (EVP_DecryptUpdate(cipher_ctx, dst, &len, src, size) != 1)
        return -1;

    if (EVP_DecryptFinal_ex(cipher_ctx, dst + len, &len) != 1)
        return -1;

    return 0;
}

int OpenSSLCryptoOperations::sha(const EVP_MD *md, const unsigned char *src, unsigned char *dst, int size) const {
    if (EVP_DigestInit_ex(md_ctx, md, nullptr) != 1)
        return -1;

    if (EVP_DigestUpdate(md_ctx, src, size) != 1)
        return -1;

    unsigned int len = 0;
    if (EVP_DigestFinal_ex(md_ctx, dst, &len) != 1)
        return -1;

    return 0;
}

int OpenSSLCryptoOperations::hmac_sha(const OSSL_PARAM *param, int dstlen, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const {
    if (EVP_MAC_init(hmac_ctx, key, key_size, param) != 1)
        return -1;

    if (EVP_MAC_update(hmac_ctx, src, size) != 1)
        return -1;

    std::size_t dst_len = dstlen;
    if (EVP_MAC_final(hmac_ctx, dst, &dst_len, dstlen) != 1)
        return -1;

    return 0;
}
