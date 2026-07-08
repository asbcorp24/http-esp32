<?php
// i.php — принимает t и h

// путь к файлу лога
$fname = __DIR__ . "/pout.txt";

// получаем параметры
$t = isset($_GET['t']) ? intval($_GET['t']) : null;
$h = isset($_GET['h']) ? intval($_GET['h']) : null;

// если параметры отсутствуют, выводим ошибку
if ($t === null || $h === null) {
    echo "ERR";
    exit;
}

// формируем строку для записи
$date = date("Y-m-d H:i:s");
$line = "[$date] t={$t}, h={$h}" . PHP_EOL;

// записываем
file_put_contents($fname, $line, FILE_APPEND | LOCK_EX);

// возвращаем простой ответ для SIM900
echo "OK";
