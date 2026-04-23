#pragma once

#include "ICryptoOperations.h"

extern "C" {
typedef struct evp_cipher_st EVP_CIPHER;
typedef struct evp_cipher_ctx_st EVP_CIPHER_CTX;
typedef struct evp_md_st EVP_MD;
typedef struct evp_md_ctx_st EVP_MD_CTX;
typedef struct evp_mac_st EVP_MAC;
typedef struct evp_mac_ctx_st EVP_MAC_CTX;
typedef struct ossl_param_st OSSL_PARAM;
}

class OpenSSLCryptoOperations : public ICryptoOperations {
public:
    OpenSSLCryptoOperations();

    ~OpenSSLCryptoOperations();

    int aes_cbc_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const override;
    int aes_cbc_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const override;

    int aes_ctr_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) override;
    int aes_ctr_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) override;

    int aes_ecb_encrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const override;
    int aes_ecb_decrypt(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const override;

    int aes_cmac(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const override;

    int sha1(const unsigned char *src, unsigned char *dst, int size) const override;
    int sha256(const unsigned char *src, unsigned char *dst, int size) const override;

    int hmac_sha1(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const override;
    int hmac_sha256(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const override;

private:
    int aes_ctr(const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const;
    int cipher_encrypt(const EVP_CIPHER *cipher, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const;
    int cipher_decrypt(const EVP_CIPHER *cipher, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size, unsigned char *iv) const;
    int sha(const EVP_MD *md, const unsigned char *src, unsigned char *dst, int size) const;
    int hmac_sha(const OSSL_PARAM *param, int dstlen, const unsigned char *src, unsigned char *dst, int size, const unsigned char *key, int key_size) const;

    EVP_CIPHER_CTX *cipher_ctx;
    EVP_MD_CTX *md_ctx;
    EVP_MAC_CTX *cmac_ctx;
    EVP_MAC_CTX *hmac_ctx;

    EVP_CIPHER *cipher_aes_cbc;
    EVP_CIPHER *cipher_aes_ctr;
    EVP_CIPHER *cipher_aes_ecb;

    EVP_MD *md_sha1;
    EVP_MD *md_sha256;

    EVP_MAC *mac_cmac;
    EVP_MAC *mac_hmac;

    OSSL_PARAM *hmac_sha1_param;
    OSSL_PARAM *hmac_sha256_param;
};
