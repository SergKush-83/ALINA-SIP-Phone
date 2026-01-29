/*
 * ALINA_SIP_Phone.cpp - Реализация главного класса библиотеки
 */

#include "ALINA_SIP_Phone.h"

// Определение глобального экземпляра
ALINASIPPhone ALINA;

ALINASIPPhone::ALINASIPPhone() : initialized(false) {
}

ALINASIPPhone::~ALINASIPPhone() {
    // Деструктор
}

bool ALINASIPPhone::begin() {
    if (initialized) {
        return true;
    }
    
    Serial.println("=== ALINA SIP Phone Library ===");
    Serial.println("Initializing...");
    
    // 1. Инициализация монитора
    monitor.init();
    monitor.startMonitoring();
    
    // 2. Инициализация устройства
    device.init();
    device.printDeviceInfo();
    
    // 3. Загрузка конфигурации
    if (!config.loadConfig()) {
        Serial.println("Using default configuration");
    }
    
    // 4. Инициализация сети
    if (!network.init()) {
        Serial.println("ERROR: Network initialization failed");
        monitor.reportError(ERROR_TYPE_NETWORK, "Network init failed");
        return false;
    }
    
    // 5. Инициализация RTP и аудио
    rtp.init(&audio, &config);
    audio.init(&rtp, &config);
    
    // 6. Инициализация SIP клиента
    sip.setSIPCredentials(
        config.getSIPUsername(),
        config.getSIPPassword(),
        config.getSIPServer(),
        config.getSIPPort()
    );
    
    if (!sip.init(&network, &audio, &rtp, &web, &config)) {
        Serial.println("ERROR: SIP client initialization failed");
        monitor.reportError(ERROR_TYPE_SIP, "SIP init failed");
        return false;
    }
    
    // 7. Инициализация веб-интерфейса
    web.init();
    
    initialized = true;
    Serial.println("=== ALINA SIP Phone Ready ===");
    
    return true;
}

void ALINASIPPhone::process() {
    if (!initialized) return;
    
    // Обработка основных компонентов
    sip.process();
    web.process();
    
    // Проверка состояния системы
    if (monitor.getState() == SYSTEM_STATE_ERROR) {
        Serial.println("CRITICAL ERROR - System recovery needed");
    }
}

bool ALINASIPPhone::makeCall(const String& number) {
    return sip.makeCall(number.c_str());
}

bool ALINASIPPhone::answerCall() {
    // Реализация будет в EnhancedSIPClient
    return false; // TODO
}

bool ALINASIPPhone::hangupCall() {
    return sip.hangupCall(-1); // -1 означает активный вызов
}

bool ALINASIPPhone::rejectCall() {
    // Реализация будет в EnhancedSIPClient
    return false; // TODO
}

bool ALINASIPPhone::registerSIP() {
    // Реализация будет в EnhancedSIPClient
    return false; // TODO
}

bool ALINASIPPhone::unregisterSIP() {
    // Реализация будет в EnhancedSIPClient
    return false; // TODO
}

bool ALINASIPPhone::isRegistered() const {
    return sip.isRegistered();
}

const char* ALINASIPPhone::getCallStatus() const {
    return "Not implemented"; // TODO
}

const char* ALINASIPPhone::getNetworkStatus() const {
    return network.isConnected() ? "Connected" : "Disconnected";
}

const char* ALINASIPPhone::getSIPStatus() const {
    return sip.isRegistered() ? "Registered" : "Not Registered";
}

int ALINASIPPhone::getActiveCalls() const {
    return sip.getActiveCallCount();
}

void ALINASIPPhone::printStatus() const {
    if (!initialized) {
        Serial.println("System not initialized");
        return;
    }
    
    Serial.println("\n=== ALINA SIP Phone Status ===");
    Serial.print("Network: ");
    Serial.println(getNetworkStatus());
    Serial.print("SIP: ");
    Serial.println(getSIPStatus());
    Serial.print("Active calls: ");
    Serial.println(getActiveCalls());
    Serial.print("Free heap: ");
    Serial.print(esp_get_free_heap_size());
    Serial.println(" bytes");
    Serial.println("============================\n");
}

void ALINASIPPhone::printSystemInfo() const {
    device.printDeviceInfo();
    monitor.printSystemStatus();
}