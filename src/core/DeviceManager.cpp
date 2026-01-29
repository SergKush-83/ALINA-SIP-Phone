/*
 * DeviceManager.cpp - Реализация управления устройством
 */

#include "DeviceManager.h"
#include "ConfigManager.h"

DeviceManager deviceManager;
extern ConfigManager configManager;

DeviceManager::DeviceManager() {
    strcpy(firmware_version, "1.0.0");
    chip_id = 0;
    flash_size = 0;
    heap_size = 0;
    generateDeviceID();
}

void DeviceManager::init() {
    // Получение информации о чипе
    chip_id = ESP.getEfuseMac();
    flash_size = ESP.getFlashChipSize();
    heap_size = ESP.getHeapSize();
    
    Serial.println("Менеджер устройства инициализирован");
}

void DeviceManager::setCustomMAC(const uint8_t* mac) {
    if (!mac) {
        Serial.println("Ошибка: Неверный MAC адрес");
        return;
    }
    
    // Проверка корректности MAC адреса
    if (mac[0] & 0x01) { // Multicast bit
        Serial.println("Ошибка: MAC адрес не может быть multicast");
        return;
    }
    
    // Установка нового MAC адреса
    esp_err_t result = esp_base_mac_addr_set(mac);
    if (result == ESP_OK) {
        Serial.printf("MAC адрес установлен: %02X:%02X:%02X:%02X:%02X:%02X\n",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        
        // Сохранение в конфигурации
        configManager.setMACAddress(mac);
        configManager.saveConfig();
    } else {
        Serial.printf("Ошибка установки MAC адреса: %d\n", result);
    }
}

bool DeviceManager::setCustomMACFromString(const char* mac_str) {
    if (!mac_str) return false;
    
    uint8_t mac[6];
    if (parseMACString(mac_str, mac)) {
        setCustomMAC(mac);
        return true;
    }
    
    return false;
}

void DeviceManager::resetToDefaultMAC() {
    // Для ESP32 нет прямой функции esp_base_mac_addr_erase
    // Вместо этого мы можем установить заводской MAC
    uint8_t default_mac[6];
    esp_read_mac(default_mac, ESP_MAC_ETH);
    esp_err_t result = esp_base_mac_addr_set(default_mac);
    
    if (result == ESP_OK) {
        Serial.println("MAC адрес сброшен к заводскому");
        
        // Сохранение в конфигурации
        configManager.setMACAddress(default_mac);
        configManager.saveConfig();
    } else {
        Serial.printf("Ошибка сброса MAC адреса: %d\n", result);
    }
}

void DeviceManager::generateDeviceID() {
    uint64_t mac = ESP.getEfuseMac();
    snprintf(device_id, sizeof(device_id), "ALINA-%04X%08X",
             (uint16_t)(mac >> 32), (uint32_t)mac);
} 

const char* DeviceManager::getDeviceID() const {
    return device_id;
}

const char* DeviceManager::getFirmwareVersion() const {
    return firmware_version;
}

uint64_t DeviceManager::getChipID() const {
    return chip_id;
}

uint32_t DeviceManager::getFlashSize() const {
    return flash_size;
}

uint32_t DeviceManager::getFreeHeap() const {
    return esp_get_free_heap_size();
}

uint32_t DeviceManager::getMinFreeHeap() const {
    return esp_get_minimum_free_heap_size();
}

void DeviceManager::printDeviceInfo() const {
    Serial.println("=== ИНФОРМАЦИЯ О УСТРОЙСТВЕ ===");
    Serial.printf("ID устройства: %s\n", device_id);
    Serial.printf("Версия прошивки: %s\n", firmware_version);
    Serial.printf("ID чипа: %012llX\n", chip_id);
    Serial.printf("Размер флеш: %lu байт\n", (unsigned long)flash_size);
    Serial.printf("Свободная память: %lu байт\n", (unsigned long)getFreeHeap());
    Serial.printf("Минимум свободной памяти: %lu байт\n", (unsigned long)getMinFreeHeap());
    
    // Текущий MAC адрес
    uint8_t current_mac[6];
    esp_read_mac(current_mac, ESP_MAC_ETH);
    char mac_str[18];
    getMACAddressString(current_mac, mac_str);
    Serial.printf("Текущий MAC адрес: %s\n", mac_str);
    
    Serial.println("===============================");
}

void DeviceManager::restart() {
    Serial.println("Перезагрузка устройства...");
    delay(1000);
    ESP.restart();
}

void DeviceManager::resetFactorySettings() {
    Serial.println("Сброс к заводским настройкам...");
    
    // Сброс конфигурации
    configManager.resetConfig();
    
    // Сброс MAC адреса
    resetToDefaultMAC();
    
    Serial.println("Заводские настройки восстановлены");
}

// Утилиты
void DeviceManager::getMACAddressString(uint8_t* mac, char* mac_str) {
    if (mac && mac_str) {
        sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
}

bool DeviceManager::parseMACString(const char* mac_str, uint8_t* mac) {
    if (!mac_str || !mac) return false;
    
    int result = sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                       &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    
    return (result == 6);
}