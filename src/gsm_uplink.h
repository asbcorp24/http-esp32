#pragma once
#include <Arduino.h>

// инициализация модема + gprs
void GsmInit();

// запуск FreeRTOS-задачи отправки
void GsmStartTask();
