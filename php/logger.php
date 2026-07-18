<?php
// logger.php
declare(strict_types=1);

const LOG_MAX_BYTES = 10485760;

function log_path(): string {
    return __DIR__ . "/server.log";
}

function reset_log_if_too_large(string $path): void {
    clearstatcache(true, $path);
    $size = @filesize($path);
    if ($size !== false && $size >= LOG_MAX_BYTES) {
        @file_put_contents($path, "");
    }
}

function log_line(string $msg, array $ctx = []): void {
    $ts = date("Y-m-d H:i:s");
    $ip = $_SERVER["REMOTE_ADDR"] ?? "-";
    $uri = $_SERVER["REQUEST_URI"] ?? "-";
    $method = $_SERVER["REQUEST_METHOD"] ?? "-";

    if (!empty($ctx)) {
        $ctxJson = json_encode($ctx, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
        $line = "[$ts] [$ip] [$method $uri] $msg | $ctxJson\n";
    } else {
        $line = "[$ts] [$ip] [$method $uri] $msg\n";
    }

    $path = log_path();
    reset_log_if_too_large($path);
    @file_put_contents($path, $line, FILE_APPEND | LOCK_EX);
}

function log_exception(Throwable $e, string $tag = "EX"): void {
    log_line($tag . " " . $e->getMessage(), [
        "file" => $e->getFile(),
        "line" => $e->getLine(),
        "trace" => substr($e->getTraceAsString(), 0, 1500),
    ]);
}

function bin_preview(string $bin, int $max = 32): string {
    $n = min(strlen($bin), $max);
    return bin2hex(substr($bin, 0, $n)) . (strlen($bin) > $max ? "..." : "");
}
