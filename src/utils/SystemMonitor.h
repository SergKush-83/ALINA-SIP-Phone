/*
 * SystemMonitor.h - Мониторинг состояния системы и обработка ошибок
 */

#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_heap_caps.h>

// Состояния системы
typedef enum {
    SYSTEM_STATE_OK,
    SYSTEM_STATE_WARNING,
    SYSTEM_STATE_ERROR,
    SYSTEM_STATE_RECOVERY
} system_state_t;

// Типы ошибок
typedef enum {
    ERROR_TYPE_NONE,
    ERROR_TYPE_NETWORK,
    ERROR_TYPE_SIP,
    ERROR_TYPE_RTP,
    ERROR_TYPE_AUDIO,
    ERROR_TYPE_MEMORY,
    ERROR_TYPE_WATCHDOG
} error_type_t;

class SystemMonitor {
private:
    system_state_t currentState;
    error_type_t lastError;
    uint32_t errorCount;
    uint32_t watchdogTimer;
    uint32_t lastWatchdogReset;
    bool watchdogEnabled;
    TaskHandle_t monitorTaskHandle;
    uint32_t taskStackHighWaterMark[10]; // Для отслеживания стека задач
    int taskCount;
    
    static void monitorTask(void* pvParameters);
    void checkSystemHealth();
    void checkMemory();
    void checkTasks();
    void resetWatchdog();
    
public:
    SystemMonitor();
    void init();
    void startMonitoring();
    void reportError(error_type_t errorType, const char* description);
    void reportTask(TaskHandle_t taskHandle, const char* taskName);
    system_state_t getState();
    error_type_t getLastError();
    void watchdogPet(); // "Погладить" watchdog
    bool isSystemStable();
    void forceRecovery();
    void printSystemStatus();
};

// Глобальный экземпляр монитора
extern SystemMonitor systemMonitor;

#endif