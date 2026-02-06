#include "crypto_aes.h"
#include <vector>
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

static void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_setup(&ctx, info, 0);
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, data, len);
  mbedtls_md_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

static void deriveKey(const String& pass, uint8_t aesKey[32], uint8_t hmacKey[32]) {
  // AES key = SHA256(pass)
  sha256((const uint8_t*)pass.c_str(), pass.length(), aesKey);

  // HMAC key = SHA256("HMAC"+pass)
  String s = "HMAC" + pass;
  sha256((const uint8_t*)s.c_str(), s.length(), hmacKey);
}

static void hmacSha256(const uint8_t key[32], const uint8_t* data, size_t len, uint8_t out[32]) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, key, 32);
  mbedtls_md_hmac_update(&ctx, data, len);
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

static void randomIV(uint8_t iv[16]) {
  for (int i = 0; i < 16; i += 4) {
    uint32_t r = esp_random();
    memcpy(iv + i, &r, 4);
  }
}

static bool pkcs7Pad(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
  const size_t block = 16;
  size_t pad = block - (inLen % block);
  if (pad == 0) pad = 16;
  out.resize(inLen + pad);
  memcpy(out.data(), in, inLen);
  memset(out.data() + inLen, (uint8_t)pad, pad);
  return true;
}

static bool pkcs7Unpad(std::vector<uint8_t>& buf) {
  if (buf.empty() || (buf.size() % 16) != 0) return false;
  uint8_t pad = buf.back();
  if (pad < 1 || pad > 16) return false;
  for (int i = 0; i < pad; i++) {
    if (buf[buf.size() - 1 - i] != pad) return false;
  }
  buf.resize(buf.size() - pad);
  return true;
}

bool aesEncryptBlob(const String& pass, const uint8_t* plain, size_t plainLen,
                    std::vector<uint8_t>& outBlob) {
  uint8_t aesKey[32], hKey[32];
  deriveKey(pass, aesKey, hKey);

  uint8_t iv[16];
  randomIV(iv);

  std::vector<uint8_t> padded;
  pkcs7Pad(plain, plainLen, padded);

  std::vector<uint8_t> cipher(padded.size());
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, aesKey, 256) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }

  uint8_t ivWork[16];
  memcpy(ivWork, iv, 16);

  if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded.size(), ivWork,
                           padded.data(), cipher.data()) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }
  mbedtls_aes_free(&aes);

  // blob = IV + cipher + HMAC(IV||cipher)
  outBlob.resize(16 + cipher.size() + 32);
  memcpy(outBlob.data(), iv, 16);
  memcpy(outBlob.data() + 16, cipher.data(), cipher.size());

  uint8_t mac[32];
  hmacSha256(hKey, outBlob.data(), 16 + cipher.size(), mac);
  memcpy(outBlob.data() + 16 + cipher.size(), mac, 32);

  return true;
}

bool aesDecryptBlob(const String& pass, const uint8_t* blob, size_t blobLen,
                    std::vector<uint8_t>& outPlain) {
  if (blobLen < 16 + 32) return false;

  size_t cipherLen = blobLen - 16 - 32;
  if ((cipherLen % 16) != 0) return false;

  const uint8_t* iv = blob;
  const uint8_t* cipher = blob + 16;
  const uint8_t* macIn = blob + 16 + cipherLen;

  uint8_t aesKey[32], hKey[32];
  deriveKey(pass, aesKey, hKey);

  // verify HMAC
  uint8_t mac[32];
  hmacSha256(hKey, blob, 16 + cipherLen, mac);
  if (memcmp(mac, macIn, 32) != 0) return false;

  outPlain.resize(cipherLen);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_dec(&aes, aesKey, 256) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }

  uint8_t ivWork[16];
  memcpy(ivWork, iv, 16);

  if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, cipherLen, ivWork,
                           cipher, outPlain.data()) != 0) {
    mbedtls_aes_free(&aes);
    return false;
  }
  mbedtls_aes_free(&aes);

  // unpad
  if (!pkcs7Unpad(outPlain)) return false;
  return true;
}
