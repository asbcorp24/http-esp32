<?php
// config.php
const DB_PATH = __DIR__ . "/server.db";

// ВАЖНО: в твоей текущей схеме "ключ общий на всех клиентов"
// Значит сервер держит один общий пароль:
const SERVER_CRYPTO_PASS = "12345678";

// Логи
const DEBUG_LOG = true;        // логировать события
const DEBUG_DUMP_JSON = false; // если true — пишет расшифрованный JSON целиком (ОПАСНО для секретов)
