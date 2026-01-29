/** EnhancedNetworkManager.cpp - Реализация улучшенного сетевого менеджера*/
#include "EnhancedNetworkManager.h"
#include "SystemMonitor.h" // Для системного мониторинга и watchdog
#include <ETH.h> // Основная библиотека Ethernet
#include <AsyncUDP.h> // Для UDP

extern SystemMonitor systemMonitor; // Глобальный экземпляр системного монитора

EnhancedNetworkManager networkManager;

// Конфигурация Ethernet для WT32-ETH01
#define ETH_PHY_TYPE ETH_PHY_LAN8720
#define ETH_PHY_ADDR 1
#define ETH_CLK_MODE ETH_CLOCK_GPIO0_IN
#define ETH_POWER_PIN 16 // WT32-ETH01 использует GPIO16 для питания PHY
#define ETH_MDC_PIN 23
#define ETH_MDIO_PIN 18

#define NETWORK_RECONNECT_TIMEOUT 15000 // 15 секунд между попытками
#define MAX_RECONNECT_ATTEMPTS 5        // Максимум 5 попыток

EnhancedNetworkManager::EnhancedNetworkManager()
    : ethConnected(false), ethWasConnected(false), lastConnectTime(0), lastDisconnectTime(0),
      reconnectAttempts(0), autoReconnect(true), networkMonitorTaskHandle(nullptr) {
    memset(localIP, 0, sizeof(localIP));
}

bool EnhancedNetworkManager::init() {
    Serial.println("Инициализация Ethernet...");
    // Попробуем инициализировать с правильными параметрами для WT32-ETH01
    if (!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, ETH_POWER_PIN, ETH_CLK_MODE)) {
        Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось инициализировать Ethernet с параметрами");
        // Вторая попытка - с другими настройками
        if (!ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_MDC_PIN, ETH_MDIO_PIN, -1, ETH_CLK_MODE)) {
            Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось инициализировать Ethernet с другими параметрами");
            // Третья попытка - без параметров
            if (!ETH.begin()) {
                Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось инициализировать Ethernet");
                return false;
            }
        }
    }

    Serial.println("Ethernet инициализирован, ожидание подключения...");

    // Запуск задачи мониторинга сети
    xTaskCreate(networkMonitorTask, "NetworkMonitor", 2048, this, 3, &networkMonitorTaskHandle);
    systemMonitor.reportTask(networkMonitorTaskHandle, "NetworkMonitor");

    // Ожидание подключения с таймаутом
    unsigned long start_time = millis();
    while (ETH.linkUp() == false && millis() - start_time < 15000) {
        delay(100);
        Serial.print(".");
        systemMonitor.watchdogPet(); // Погладить watchdog
    }

    if (ETH.linkUp()) {
        ethConnected = true;
        ethWasConnected = true;
        lastConnectTime = millis();
        // ВАЖНО: Не вызываем sprintf(localIP, ...) здесь, потому что IP может быть получен позже
        // Мы получим IP в задаче мониторинга или при вызове getLocalIP()
        Serial.println();
        Serial.println("Ethernet физически подключен");
        // Попробуем получить IP, если он уже есть
        if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
            sprintf(localIP, "%s", ETH.localIP().toString().c_str());
        } else {
            strcpy(localIP, "0.0.0.0"); // Временное значение
        }
        Serial.print("Текущий IP (если есть): ");
        Serial.println(localIP);
        return true;
    } else {
        Serial.println();
        Serial.println("Ethernet не подключен в течение таймаута");
        systemMonitor.reportError(ERROR_TYPE_NETWORK, "Ethernet connection timeout");
        return false;
    }
}

void EnhancedNetworkManager::networkMonitorTask(void* pvParameters) {
    EnhancedNetworkManager* netMgr = (EnhancedNetworkManager*)pvParameters;
    while (1) {
        // Погладить watchdog
        systemMonitor.watchdogPet();

        // Проверка состояния соединения
        if (!netMgr->isConnected() && netMgr->autoReconnect) {
            if (millis() - netMgr->lastDisconnectTime > NETWORK_RECONNECT_TIMEOUT) {
                netMgr->attemptReconnect();
            }
        }

        // Отправка статуса системному монитору
        if (!netMgr->isConnected()) {
            static uint32_t lastNetworkErrorReport = 0;
            if (millis() - lastNetworkErrorReport > 30000) { // Отчет раз в 30 секунд
                systemMonitor.reportError(ERROR_TYPE_NETWORK, "Ethernet disconnected");
                lastNetworkErrorReport = millis();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500)); // Проверка каждые 500 мс
    }
    
}

void EnhancedNetworkManager::attemptReconnect() {
    Serial.println("Попытка переподключения к Ethernet...");
    if (reconnectAttempts < MAX_RECONNECT_ATTEMPTS) {
        // ETH.begin уже вызван в init, повторный вызов не нужен
        // Просто ждем, пока физическое соединение и IP не восстановятся
        if (ETH.linkUp()) {
            // Ждем получения IP
            unsigned long start_time = millis();
            while (ETH.localIP() == IPAddress(0, 0, 0, 0) && millis() - start_time < 15000) {
                delay(100);
                systemMonitor.watchdogPet();
            }
            if (ETH.localIP() != IPAddress(0, 0, 0, 0)) {
                ethConnected = true;
                lastConnectTime = millis();
                sprintf(localIP, "%s", ETH.localIP().toString().c_str());
                reconnectAttempts = 0; // Сброс счетчика
                Serial.println("Подключение восстановлено");
                systemMonitor.reportError(ERROR_TYPE_NETWORK, "Ethernet reconnected"); // Отчет о восстановлении
            } else {
                Serial.println("Подключение не восстановлено (IP не получен)");
                reconnectAttempts++;
                systemMonitor.reportError(ERROR_TYPE_NETWORK, "Reconnection failed");
            }
        } else {
            Serial.println("Подключение не восстановлено (PHY link down)");
            reconnectAttempts++;
            systemMonitor.reportError(ERROR_TYPE_NETWORK, "Reconnection failed");
        }
    } else {
        Serial.println("Превышено максимальное количество попыток подключения");
        systemMonitor.reportError(ERROR_TYPE_NETWORK, "Max reconnection attempts exceeded");
    }
}

bool EnhancedNetworkManager::isConnected() {
    bool link_ok = ETH.linkUp();
    bool ip_ok = ETH.localIP() != IPAddress(0, 0, 0, 0);
    bool connected = link_ok && ip_ok;

    // Обновление состояния подключения и времени
    if (connected && !ethConnected) {
        // Только что подключились
        ethConnected = true;
        ethWasConnected = true;
        lastConnectTime = millis();
        sprintf(localIP, "%s", ETH.localIP().toString().c_str());
        Serial.println("Сеть подключена");
        Serial.printf("IP: %s\n", localIP);
    } else if (!connected && ethConnected) {
        // Только что отключились
        ethConnected = false;
        lastDisconnectTime = millis();
        Serial.println("Сеть отключена");
    }

    return connected;
}

const char* EnhancedNetworkManager::getLocalIP() {
    // Всегда проверяем подключение и обновляем IP, если нужно
    if (isConnected()) {
        // Убедимся, что локальный IP обновлен
        sprintf(localIP, "%s", ETH.localIP().toString().c_str());
        return localIP;
    }
    // Если не подключены, возвращаем 0.0.0.0
    strcpy(localIP, "0.0.0.0");
    return localIP;
}

void EnhancedNetworkManager::enableAutoReconnect(bool enable) {
    autoReconnect = enable;
}

void EnhancedNetworkManager::forceReconnect() {
    Serial.println("Принудительное переподключение");
    reconnectAttempts = 0;
    attemptReconnect();
}

uint32_t EnhancedNetworkManager::getUptime() {
    if (isConnected()) {
        return millis() - lastConnectTime;
    }
    return 0;
}

uint32_t EnhancedNetworkManager::getDowntime() {
    if (!isConnected() && ethWasConnected) {
        return millis() - lastDisconnectTime;
    }
    return 0;
}

void EnhancedNetworkManager::printNetworkStatus() {
    // Serial.println("=== СОСТОЯНИЕ СЕТИ ===");
    // Serial.printf("Подключен: %s\n", isConnected() ? "ДА" : "НЕТ");
    // Serial.printf("IP адрес: %s\n", getLocalIP());
    // Serial.printf("Время работы: %lu секунд\n", (unsigned long)(getUptime() / 1000));
    // Serial.printf("Время простоя: %lu секунд\n", (unsigned long)(getDowntime() / 1000));
}