/*
 * EnhancedNetworkManager.h - Улучшенное управление сетевыми соединениями
 */

#ifndef ENHANCED_NETWORK_MANAGER_H
#define ENHANCED_NETWORK_MANAGER_H

#include <Arduino.h>
#include <ETH.h>  // Только Ethernet, без WiFi
#include <AsyncUDP.h>
#include "SystemMonitor.h"

#define NETWORK_RECONNECT_TIMEOUT 30000 // 30 секунд
#define NETWORK_MAX_RECONNECT_ATTEMPTS 5

class EnhancedNetworkManager {
private:
    char localIP[16];
    bool ethConnected;
    bool ethWasConnected;
    uint32_t lastConnectTime;
    uint32_t lastDisconnectTime;
    uint32_t reconnectAttempts;
    bool autoReconnect;
    TaskHandle_t networkMonitorTaskHandle;
    
    static void networkMonitorTask(void* pvParameters);
    void attemptReconnect();
    
public:
    EnhancedNetworkManager();
    bool init();
    bool isConnected();
    const char* getLocalIP();
    void enableAutoReconnect(bool enable);
    void forceReconnect();
    uint32_t getUptime();
    uint32_t getDowntime();
    void printNetworkStatus();
    bool isEthConnected() const { return ethConnected; } // Добавляем геттер
    
    AsyncUDP udp;
};

extern EnhancedNetworkManager networkManager;

#endif