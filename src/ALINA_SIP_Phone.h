/*
 * ALINA_SIP_Phone.h - Главный заголовочный файл библиотеки
 * Профессиональная SIP-телефония для ESP32 с Ethernet
 */

#ifndef ALINA_SIP_PHONE_H
#define ALINA_SIP_PHONE_H

#include "Arduino.h"
#include "core/DeviceManager.h"
#include "core/EnhancedNetworkManager.h"
#include "core/EnhancedSIPClient.h"
#include "core/RTPManager.h"
#include "audio/AudioManager.h"
#include "config/ConfigManager.h"
#include "web/WebInterface.h"
#include "utils/SystemMonitor.h"

class ALINASIPPhone {
public:
    // Конструктор/деструктор
    ALINASIPPhone();
    ~ALINASIPPhone();
    
    // Основные методы
    bool begin();
    void process();
    
    // Управление вызовами
    bool makeCall(const String& number);
    bool answerCall();
    bool hangupCall();
    bool rejectCall();
    
    // Управление регистрацией
    bool registerSIP();
    bool unregisterSIP();
    bool isRegistered() const;
    
    // Геттеры состояния
    const char* getCallStatus() const;
    const char* getNetworkStatus() const;
    const char* getSIPStatus() const;
    int getActiveCalls() const;
    
    // Геттеры компонентов
    DeviceManager& getDevice() { return device; }
    EnhancedNetworkManager& getNetwork() { return network; }
    EnhancedSIPClient& getSIP() { return sip; }
    RTPManager& getRTP() { return rtp; }
    AudioManager& getAudio() { return audio; }
    ConfigManager& getConfig() { return config; }
    WebInterface& getWeb() { return web; }
    SystemMonitor& getMonitor() { return monitor; }
    
    // Статистика
    void printStatus() const;
    void printSystemInfo() const;
    
private:
    DeviceManager device;
    EnhancedNetworkManager network;
    RTPManager rtp;
    AudioManager audio;
    ConfigManager config;
    WebInterface web;
    EnhancedSIPClient sip;
    SystemMonitor monitor;
    
    bool initialized;
    
    // Запрет копирования
    ALINASIPPhone(const ALINASIPPhone&) = delete;
    ALINASIPPhone& operator=(const ALINASIPPhone&) = delete;
};

// Глобальный экземпляр для удобства
extern ALINASIPPhone ALINA;

#endif // ALINA_SIP_PHONE_H