#pragma once
#include <Arduino.h>
#include <vector> 
// Выход: blob = [16 bytes IV][ciphertext][32 bytes HMAC]
// HMAC считается по (IV||ciphertext) с ключом hmacKey = SHA256("HMAC"+pass)
bool aesEncryptBlob(const String& pass, const uint8_t* plain, size_t plainLen,
                    std::vector<uint8_t>& outBlob);

bool aesDecryptBlob(const String& pass, const uint8_t* blob, size_t blobLen,
                    std::vector<uint8_t>& outPlain);
