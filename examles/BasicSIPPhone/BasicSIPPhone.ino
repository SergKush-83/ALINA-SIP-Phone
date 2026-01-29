#include <ALINA_SIP_Phone.h>

// Конфигурация
#define SIP_USER "1001"
#define SIP_PASSWORD "password123"
#define SIP_SERVER "192.168.1.100"
#define SIP_PORT 5060

ALINASIPPhone phone;

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("ALINA SIP Phone Initializing...");
    
    // Настройка конфигурации
    phone.getConfig().setSIPCredentials(SIP_USER, SIP_PASSWORD, SIP_SERVER, SIP_PORT);
    phone.getConfig().setDeviceName("ALINA-Desk-Phone");
    
    // Запуск системы
    if (phone.begin()) {
        Serial.println("SIP Phone initialized successfully!");
    } else {
        Serial.println("Failed to initialize SIP Phone!");
        while (1) delay(1000);
    }
}

void loop() {
    // Основной цикл обработки
    phone.process();
    
    // Пример обработки команд с Serial
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        
        if (cmd.startsWith("call ")) {
            String number = cmd.substring(5);
            if (phone.getSIP().makeCall(number.c_str())) {
                Serial.println("Dialing: " + number);
            } else {
                Serial.println("Failed to make call");
            }
        } else if (cmd == "hangup") {
            phone.getSIP().hangupCall();
            Serial.println("Call terminated");
        } else if (cmd == "status") {
            printStatus();
        } else if (cmd == "answer") {
            auto call = phone.getSIP().getActiveCall();
            if (call) {
                phone.getSIP().answerCall(call->id);
            }
        }
    }
    
    delay(10); // Небольшая задержка для стабильности
}

void printStatus() {
    Serial.println("\n=== SIP Phone Status ===");
    Serial.print("Network: ");
    Serial.println(phone.getNetwork().isConnected() ? "Connected" : "Disconnected");
    Serial.print("SIP: ");
    Serial.println(phone.getSIP().getStateString());
    Serial.print("Active calls: ");
    Serial.println(phone.getSIP().getActiveCallCount());
    
    if (phone.getSIP().isRegistered()) {
        Serial.print("Registered for: ");
        Serial.print(phone.getSIP().getUptime() / 1000);
        Serial.println(" seconds");
    }
    Serial.println("======================\n");
}