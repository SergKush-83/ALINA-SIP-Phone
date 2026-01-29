/*
 * DeviceManager.h - Управление устройством WT32-ETH01
 */

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <Arduino.h>
#include <esp_system.h>
#include <esp_mac.h>

class DeviceManager {
private:
    char device_id[32];
    char firmware_version[16];
    uint64_t chip_id;
    uint32_t flash_size;
    uint32_t heap_size;
    
    void generateDeviceID();
    
public:
    DeviceManager();
    
    void init();
    void setCustomMAC(const uint8_t* mac);
    bool setCustomMACFromString(const char* mac_str);
    void resetToDefaultMAC();
    
    const char* getDeviceID() const;
    const char* getFirmwareVersion() const;
    uint64_t getChipID() const;
    uint32_t getFlashSize() const;
    uint32_t getFreeHeap() const;
    uint32_t getMinFreeHeap() const;
    
    void printDeviceInfo() const;
    void restart();
    void resetFactorySettings();
    
    // Утилиты
    static void getMACAddressString(uint8_t* mac, char* mac_str);
    static bool parseMACString(const char* mac_str, uint8_t* mac);
};

extern DeviceManager deviceManager;

#endif