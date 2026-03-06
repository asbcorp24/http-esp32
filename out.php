<?php

$fname = "pout.txt";

?>
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<title>Лог</title>
</head>
<body>
<pre>
<?php
if (file_exists($fname)) {
    echo htmlspecialchars(file_get_contents($fname));
} else {
    echo "Файл $fname пока не существует";
}
?>
</pre>
</body>
</html>
