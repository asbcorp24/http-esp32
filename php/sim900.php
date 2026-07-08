<?php

$temp = $_GET['temp'] ?? null;
$hum  = $_GET['hum']  ?? null;

if ($temp === null || $hum === null) {
    echo "ERR";
    exit;
}

$date = date("Y-m-d H:i:s");
$line = "[$date] temp=$temp, hum=$hum" . PHP_EOL;

file_put_contents("pout.txt", $line, FILE_APPEND | LOCK_EX);

echo "OK";
