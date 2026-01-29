/*
 * SystemMonitor.cpp - Реализация мониторинга системы
 */

#include "SystemMonitor.h"

SystemMonitor systemMonitor;

SystemMonitor::SystemMonitor() : 
    currentState(SYSTEM_STATE_OK),
    lastError(ERROR_TYPE_NONE),
    errorCount(0),
    watchdogTimer(0),
    lastWatchdogReset(0),
    watchdogEnabled(true),
    monitorTaskHandle(nullptr),
    taskCount(0) {
}

void SystemMonitor::init() {
    currentState = SYSTEM_STATE_OK;
    lastError = ERROR_TYPE_NONE;
    errorCount = 0;
    watchdogTimer = millis();
    lastWatchdogReset = millis();
    
    Serial.println("Системный монитор инициализирован");
}

void SystemMonitor::startMonitoring() {
    xTaskCreate(monitorTask, "SystemMonitor", 4096, this, 5, &monitorTaskHandle);
    Serial.println("Мониторинг системы запущен");
}

void SystemMonitor::monitorTask(void* pvParameters) {
    SystemMonitor* monitor = (SystemMonitor*)pvParameters;
    
    while (1) {
        monitor->checkSystemHealth();
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Проверка каждые 5 секунд
    }
}

void SystemMonitor::checkSystemHealth() {
    // Проверка watchdog
    if (watchdogEnabled && (millis() - lastWatchdogReset > 30000)) { // 30 секунд
        reportError(ERROR_TYPE_WATCHDOG, "Watchdog timeout");
        forceRecovery();
        return;
    }
    
    // Проверка памяти
    checkMemory();
    
    // Проверка задач
    checkTasks();
    
    // Если были ошибки, но система стабильна - возвращаемся в нормальное состояние
    if (currentState == SYSTEM_STATE_ERROR && errorCount < 3) {
        currentState = SYSTEM_STATE_WARNING;
    }
    
    if (currentState == SYSTEM_STATE_WARNING && errorCount == 0) {
        currentState = SYSTEM_STATE_OK;
    }
}

void SystemMonitor::checkMemory() {
    uint32_t freeHeap = esp_get_free_heap_size();
    uint32_t minFreeHeap = esp_get_minimum_free_heap_size();
    
    if (freeHeap < 20000) { // Меньше 20KB свободной памяти
        reportError(ERROR_TYPE_MEMORY, "Low memory");
    }
    
    if (minFreeHeap < 10000) { // Минимум был меньше 10KB
        reportError(ERROR_TYPE_MEMORY, "Critical low memory");
    }
}

void SystemMonitor::checkTasks() {
    // Проверка стека задач
    for (int i = 0; i < taskCount; i++) {
        if (taskStackHighWaterMark[i] < 512) { // Меньше 512 байт свободного стека
            Serial.printf("Предупреждение: низкий уровень стека задачи %d\n", i);
        }
    }
}

void SystemMonitor::reportError(error_type_t errorType, const char* description) {
    lastError = errorType;
    errorCount++;
    
    Serial.printf("СИСТЕМНАЯ ОШИБКА [%d]: %s (Количество: %lu)\n", 
                  (int)errorType, description, (unsigned long)errorCount);
    
    if (errorCount >= 5) {
        currentState = SYSTEM_STATE_ERROR;
        Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Система переходит в режим восстановления");
        forceRecovery();
    } else if (errorCount >= 3) {
        currentState = SYSTEM_STATE_WARNING;
    }
}


void SystemMonitor::reportTask(TaskHandle_t taskHandle, const char* taskName) {
    if (taskCount < 10) {
        taskStackHighWaterMark[taskCount] = uxTaskGetStackHighWaterMark(taskHandle);
        taskCount++;
        Serial.printf("Задача %s зарегистрирована для мониторинга\n", taskName);
    }
}

system_state_t SystemMonitor::getState() {
    return currentState;
}

error_type_t SystemMonitor::getLastError() {
    return lastError;
}

void SystemMonitor::watchdogPet() {
    lastWatchdogReset = millis();
}

void SystemMonitor::resetWatchdog() {
    lastWatchdogReset = millis();
    watchdogTimer = millis();
}

bool SystemMonitor::isSystemStable() {
    return (currentState == SYSTEM_STATE_OK || currentState == SYSTEM_STATE_WARNING);
}

void SystemMonitor::forceRecovery() {
    currentState = SYSTEM_STATE_RECOVERY;
    Serial.println("ЗАПУСК ПРОЦЕДУРЫ ВОССТАНОВЛЕНИЯ СИСТЕМЫ");
    
    // Здесь можно добавить процедуры восстановления
    // Например: перезапуск сетевого соединения, очистка памяти и т.д.
    
    // Для тестирования - перезагрузка через 5 секунд
    Serial.println("Перезагрузка через 5 секунд...");
    delay(5000);
    esp_restart();
}

void SystemMonitor::printSystemStatus() {
    Serial.println("=== СОСТОЯНИЕ СИСТЕМЫ ===");
    Serial.printf("Состояние: %d\n", currentState);
    Serial.printf("Последняя ошибка: %d\n", lastError);
    Serial.printf("Количество ошибок: %lu\n", (unsigned long)errorCount);
    Serial.printf("Свободная память: %lu байт\n", (unsigned long)esp_get_free_heap_size());
    Serial.printf("Минимум свободной памяти: %lu байт\n", (unsigned long)esp_get_minimum_free_heap_size());
    Serial.printf("Watchdog: %s\n", watchdogEnabled ? "ВКЛ" : "ВЫКЛ");
    Serial.println("========================");
}