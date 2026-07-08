<?php
// logger.php
declare(strict_types=1);

function log_path(): string {
    // Папка должна быть доступна на запись (проверь права)
    return __DIR__ . "/server.log";
}

function log_line(string $msg, array $ctx = []): void {
    $ts = date("Y-m-d H:i:s");
    $ip = $_SERVER["REMOTE_ADDR"] ?? "-";
    $uri = $_SERVER["REQUEST_URI"] ?? "-";
    $method = $_SERVER["REQUEST_METHOD"] ?? "-";

    if (!empty($ctx)) {
        // Безопасный JSON, чтобы не сломать лог
        $ctxJson = json_encode($ctx, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        $line = "[$ts] [$ip] [$method $uri] $msg | $ctxJson\n";
    } else {
        $line = "[$ts] [$ip] [$method $uri] $msg\n";
    }

    // FILE_APPEND + LOCK_EX чтобы не перемешивалось
    @file_put_contents(log_path(), $line, FILE_APPEND | LOCK_EX);
}

function log_exception(Throwable $e, string $tag = "EX"): void {
    log_line($tag . " " . $e->getMessage(), [
        "file" => $e->getFile(),
        "line" => $e->getLine(),
        "trace" => substr($e->getTraceAsString(), 0, 1500),
    ]);
}

// Выведем аккуратно hex для первых байт бинарника
function bin_preview(string $bin, int $max = 32): string {
    $n = min(strlen($bin), $max);
    return bin2hex(substr($bin, 0, $n)) . (strlen($bin) > $max ? "..." : "");
}
