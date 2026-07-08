<?php
// crypto.php
declare(strict_types=1);

// === SHA256 raw bytes ===
function sha256_raw(string $data): string {
    return hash('sha256', $data, true); // raw bytes 32
}

// === derive AES + HMAC keys ===
function derive_keys(string $pass): array {
    $aesKey  = sha256_raw($pass);            // 32 bytes
    $hmacKey = sha256_raw("HMAC" . $pass);   // 32 bytes
    return [$aesKey, $hmacKey];
}

// === constant-time compare ===
function ct_equals(string $a, string $b): bool {
    if (function_exists('hash_equals')) return hash_equals($a, $b);
    if (strlen($a) !== strlen($b)) return false;
    $r = 0;
    for ($i=0; $i<strlen($a); $i++) $r |= (ord($a[$i]) ^ ord($b[$i]));
    return $r === 0;
}

/**
 * Decrypt your blob format:
 * blob = IV(16) || CIPHER(n*16) || HMAC(32) where HMAC = HMAC-SHA256(hmacKey, IV||CIPHER)
 *
 * @return array [ok(bool), plain(string), err(string)]
 */
function aes_decrypt_blob(string $pass, string $blob): array {
    $len = strlen($blob);
    if ($len < (16 + 32)) return [false, "", "blob_too_small"];

    $cipherLen = $len - 16 - 32;
    if (($cipherLen % 16) !== 0) return [false, "", "cipher_len_bad"];

    $iv     = substr($blob, 0, 16);
    $cipher = substr($blob, 16, $cipherLen);
    $macIn  = substr($blob, 16 + $cipherLen, 32);

    [$aesKey, $hmacKey] = derive_keys($pass);

    // verify HMAC over IV||CIPHER
    $macCalc = hash_hmac('sha256', $iv . $cipher, $hmacKey, true);
    if (!ct_equals($macCalc, $macIn)) {
        return [false, "", "hmac_bad"];
    }

    // decrypt AES-256-CBC (OpenSSL removes PKCS7 automatically)
    $plain = openssl_decrypt($cipher, 'AES-256-CBC', $aesKey, OPENSSL_RAW_DATA, $iv);
    if ($plain === false) return [false, "", "decrypt_fail"];

    return [true, $plain, ""];
}
