/** EnhancedSIPClient.cpp - SIP-клиент для WT32-ETH01
 * Поддержка: REGISTER (Digest), 2 вызова, корректный ACK, Record-Route, правильная обработка CSeq
 * Исправлены логические ошибки, улучшена стабильность.
 * Исправлено: Ожидание подключения сети перед регистрацией/отправкой.
 */
#include "EnhancedSIPClient.h"
#include "WebInterface.h"
#include "ConfigManager.h"
#include <esp_random.h>
#include <mbedtls/md5.h>
#include <WiFi.h>          // Для esp_random()
#include <cstring> // Для memset, strncpy, snprintf, strtok_r
#include <cstdio>  // Для snprintf
#include <cstdlib> // Для atoi



EnhancedSIPClient sipClient;

EnhancedSIPClient::EnhancedSIPClient() 
    : networkManager(nullptr), audioManager(nullptr), rtpManager(nullptr),
      webInterface(nullptr), configManager(nullptr),
      sip_state(SIP_STATE_INITIALIZING), sip_registered(false),
      last_register_success(0), register_expires(3600), require_auth(false),
      active_calls(0), sip_cseq(1), max_calls(0), calls(nullptr) {
    
    // Инициализация массивов
    memset(sip_user, 0, sizeof(sip_user));
    memset(sip_password, 0, sizeof(sip_password));
    memset(sip_server, 0, sizeof(sip_server));
    memset(call_id, 0, sizeof(call_id));
    memset(&auth_info, 0, sizeof(auth_info));
}

EnhancedSIPClient::~EnhancedSIPClient() {
    // Освобождаем динамически выделенную память
    if (calls) {
        delete[] calls;
        calls = nullptr;
    }
}


void EnhancedSIPClient::init(EnhancedNetworkManager* netMgr, AudioManager* audioMgr,
                            RTPManager* rtpMgr, WebInterface* webInt, ConfigManager* cfgMgr) {
    
    // Сохраняем указатели на зависимости
    networkManager = netMgr;
    audioManager = audioMgr; 
    rtpManager = rtpMgr;
    webInterface = webInt;
    configManager = cfgMgr;
    
    Serial.println("SIP: Инициализация с dependency injection");
    
    // Валидация указателей
    if (!networkManager) {
        Serial.println("SIP: ОШИБКА - networkManager не инициализирован");
        sip_state = SIP_STATE_ERROR;
        return;
    }
    
    if (!configManager) {
        Serial.println("SIP: ОШИБКА - configManager не инициализирован");
        sip_state = SIP_STATE_ERROR;
        return;
    }
    
    max_calls = configManager->getMaxCalls();
    if (max_calls <= 0) {
        max_calls = 5; // значение по умолчанию
    }

    // Освобождаем предыдущий массив, если он был
    if (calls) {
        delete[] calls;
    }
    
    // Выделяем память для массива вызовов
    calls = new call_t[max_calls];
    if (!calls) {
        Serial.println("SIP: ОШИБКА - не удалось выделить память для вызовов");
        sip_state = SIP_STATE_ERROR;
        return;
    }
    if (configManager) {
    // Установить register_expires из конфига при инициализации
    register_expires = configManager->getSIPExpires();
    if (register_expires <= 0) {
        register_expires = 3600; // значение по умолчанию на случай, если в конфиге 0 или отрицательное
    }
    Serial.printf("SIP: Установлен Expires из конфига: %d секунд\n", register_expires);
    }
   // Инициализируем все вызовы
    for (int i = 0; i < max_calls; i++) {
        resetCall(&calls[i]);
        calls[i].id = i;
    }
    

    // Проверка учетных данных через configManager
    if (!validateSIPCredentials()) {
        Serial.println("SIP: ОШИБКА - невалидные учетные данные SIP");
        sip_state = SIP_STATE_ERROR;
        return;
    }
    
    // Настройка UDP порта для SIP
    if (!networkManager->udp.listen(SIP_PORT)) {
        Serial.printf("SIP: ОШИБКА - не удалось открыть UDP порт %d\n", SIP_PORT);
        sip_state = SIP_STATE_ERROR;
        return;
    }
    
    // Обработчик входящих пакетов
      networkManager->udp.onPacket([this](AsyncUDPPacket& packet) { // <-- Изменено: принимает &
        this->handleIncomingPacket(packet); // <-- Изменено: вызывает handleIncomingPacket
    });

    Serial.printf("SIP: Успешно инициализирован на порту %d\n", SIP_PORT);
    Serial.printf("SIP: Сервер: %s:%d, Пользователь: %s\n", 
                  sip_server, sip_server_port, sip_user);
    
    sip_state = SIP_STATE_INITIALIZING;
}

// EnhancedSIPClient.cpp (внутри класса)

// EnhancedSIPClient.cpp (внутри класса)

void EnhancedSIPClient::process() {
    // Serial.printf("SIP: Process called. networkManager ptr: 0x%p, Network connected (via ptr): %s, SIP State: %d\n",
    //               (void*)networkManager,
    //               networkManager && networkManager->isConnected() ? "YES" : "NO",
    //               sip_state);

    // Проверяем, подключена ли сеть
    if (!networkManager) {
        Serial.println("SIP: networkManager указатель равен nullptr!");
        return;
    }

    bool net_connected = networkManager->isConnected();
    // Serial.printf("SIP: networkManager->isConnected() вернул: %s\n", net_connected ? "YES" : "NO");
    // Serial.printf("SIP: ETH.linkUp() возвращает: %s\n", ETH.linkUp() ? "YES" : "NO");
    // Serial.printf("SIP: ethConnected (внутри networkManager) равен: %s\n", networkManager->isEthConnected() ? "true" : "false");
    // Serial.printf("SIP: SIP Local IP: %s\n", getLocalIP()); // Используем публичный метод

    if (!net_connected) {
        Serial.println("SIP: Сеть не подключена, ожидание...\n");
        sip_state = SIP_STATE_INITIALIZING; // Сбросим состояние, если сеть отключена
        sip_registered = false; // Сбросить статус регистрации
        return;
    }

    const char* local_ip = getLocalIP(); // Используем публичный метод
    if (!local_ip || strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: Локальный IP 0.0.0.0, ожидание получения IP...\n");
        sip_state = SIP_STATE_INITIALIZING; // Сбросим состояние, если IP не получен
        sip_registered = false; // Сбросить статус регистрации
        if (audioManager) {
            audioManager->stopTasks();
            Serial.println("SIP: AudioManager задачи остановлены (потеря сети)");
        }
        return;
    }
    
    //Serial.println("SIP: Сеть подключена (внутри process).\n");

    // --- УПРОЩЕННАЯ ЛОГИКА РЕГИСТРАЦИИ ---
    
    // 1. Начальная регистрация
    if (sip_state == SIP_STATE_INITIALIZING) {
        Serial.println("SIP: Сеть подключена, запуск регистрации...\n");
        sip_state = SIP_STATE_REGISTERING;
        handleRegistration(false); // Первая попытка без аутентификации
        return; // Выйти после вызова
    }
    
    // 2. Обработка требования аутентификации (только если еще не зарегистрированы)
    if (sip_state == SIP_STATE_REGISTERING && require_auth && strlen(auth_info.nonce) > 0 && !sip_registered) {
        Serial.println("SIP: Требуется аутентификация, отправка REGISTER с Digest\n");
        handleRegistration(true); // С аутентификацией
        return; // Выйти после вызова
    }

    // 3. Запуск аудио задач после успешной регистрации
    if (sip_state == SIP_STATE_REGISTERED && !audio_tasks_started) {
        static bool audio_tasks_started = false;
        if (!audio_tasks_started && audioManager) {
            audioManager->startTasks();
            audio_tasks_started = true;
            Serial.println("SIP: AudioManager задачи запущены (состояние REGISTERED)");
        }
    }  

    // 4. ПРОВЕРКА АКТИВНЫХ ВЫЗОВОВ ПЕРЕД ПЕРЕРЕГИСТРАЦИЕЙ
    bool has_active_calls = false;
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state == CALL_STATE_ACTIVE || 
            calls[i].state == CALL_STATE_RINGING || 
            calls[i].state == CALL_STATE_WAITING_FOR_ACK) {
            has_active_calls = true;
            break;
        }
    }


    // 5. Повторная регистрация по таймеру (только если уже зарегистрированы)
    if (sip_registered && register_expires >  0&& !has_active_calls) { // Добавили проверку, что register_expires > 0
    unsigned long registration_timeout;
    if (register_expires > 300) {
        // Если Expires больше 5 минут, ждем (Expires - 5 минут)
        registration_timeout = (register_expires - 300) * 1000UL;
    } else {
        // Если Expires <= 5 минут, ждем половину времени (или другую логику)
        // Например, ждем половину от register_expires
        registration_timeout = (register_expires / 2) * 1000UL;
        //Serial.printf("SIP: ВНИМАНИЕ: Expires (%d) <= 300 сек. Используем таймаут для перерегистрации: %lu мс\n", register_expires, registration_timeout);
    }

    if (millis() - last_register_success > registration_timeout) {
        Serial.println("SIP: Требуется повторная регистрация по таймеру");
        sip_state = SIP_STATE_REGISTERING;
        sip_registered = false;
        require_auth = false; // Сбросить флаг аутентификации для новой попытки
        memset(&auth_info, 0, sizeof(auth_info)); // Очистить информацию об аутентификации
        handleRegistration(false); // Начинаем без аутентификации
    }
}
    // --- ОБРАБОТКА ВЫЗОВОВ ---

    // Обработка таймаутов ожидания ACK
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state == CALL_STATE_WAITING_FOR_ACK) {
            // Если ждем ACK больше 2 секунд, принудительно активируем вызов
            if (millis() - calls[i].last_activity > 2000) {
                Serial.printf("SIP: Таймаут ожидания ACK для вызова %d, принудительно активируем\n", i);
                calls[i].state = CALL_STATE_ACTIVE;
                calls[i].last_activity = millis();
                
                // Запускаем аудиопоток
                if (audioManager) {
                    // audioManager->startStream(calls[i].local_rtp_port, calls[i].remote_ip, 
                    //                         calls[i].remote_rtp_port, calls[i].ssrc);
                    // Serial.printf("SIP: Аудиопоток запущен для вызова %d\n", i);
                }
            }
        }
        
        // Общие таймауты вызовов (60 секунд)
        if (calls[i].state != CALL_STATE_IDLE) {
            if (millis() - calls[i].last_activity > 60000) {
                Serial.printf("SIP: Таймаут вызова %d (состояние: %d)\n", i, calls[i].state);
                if (calls[i].state == CALL_STATE_INCOMING || calls[i].state == CALL_STATE_RINGING) {
                    // Ответить 480 Temporarily Unavailable или 408 Request Timeout
                    sendResponse(408, "Request Timeout", calls[i].remote_ip, calls[i].remote_sip_port, 
                                nullptr, calls[i].to_tag, false, 0);
                }
                resetCall(&calls[i]);
                active_calls = max(0, active_calls - 1);
            }
        }
    }

    // Логика для отправки keepalive (опционально)
    // static unsigned long last_keepalive = 0;
    // if (sip_registered && millis() - last_keepalive > configManager->getKeepaliveInterval()) {
    //     sendKeepalive();
    //     last_keepalive = millis();
    // }
}


// EnhancedSIPClient.cpp (внутри класса)

void EnhancedSIPClient::handleIncomingPacket(AsyncUDPPacket& packet) {
    if (packet.length() == 0) return;

    // Получаем IP и порт отправителя
    IPAddress remoteIP = packet.remoteIP();
    uint16_t remotePort = packet.remotePort();
    
    // Проверяем валидность IP
    if (remoteIP == INADDR_NONE) {
        Serial.println("SIP: Ошибка - невалидный IP отправителя\n");
        return;
    }
    
    String remoteIPStr = remoteIP.toString();
    const char* remote_ip = remoteIPStr.c_str();

    // Проверяем, что IP не пустой
    if (strlen(remote_ip) == 0 || strcmp(remote_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: Ошибка - пустой IP отправителя\n");
        return;
    }

    char buffer[1024];
    size_t len = packet.length();
    if (len >= sizeof(buffer)) {
        len = sizeof(buffer) - 1;
        Serial.println("SIP: Warning: Packet too long, truncated.\n");
    }
    memcpy(buffer, packet.data(), len);
    buffer[len] = '\0';

    // +++ ДЕТАЛЬНАЯ ОТЛАДКА +++
    Serial.println("==========================================");
    Serial.printf("SIP: Получен пакет от %s:%d\n", remote_ip, remotePort);
    Serial.printf("SIP: Длина пакета: %d байт\n", len);
    Serial.println("SIP: Содержимое пакета:");
    Serial.println("------------------------------------------");
    Serial.println(buffer);
    Serial.println("------------------------------------------");
    
    // Анализ типа сообщения
    if (strncmp(buffer, "SIP/2.0", 7) == 0) {
        Serial.println("SIP: Тип: RESPONSE");
        // Дополнительный анализ кода ответа
        if (strstr(buffer, "SIP/2.0 200")) {
            Serial.println("SIP: Код ответа: 200 OK");
            if (strstr(buffer, "INVITE")) Serial.println("SIP: Для: INVITE");
            if (strstr(buffer, "REGISTER")) Serial.println("SIP: Для: REGISTER");
            if (strstr(buffer, "BYE")) Serial.println("SIP: Для: BYE");
        }
        else if (strstr(buffer, "SIP/2.0 401")) {
            Serial.println("SIP: Код ответа: 401 Unauthorized");
        }
        else if (strstr(buffer, "SIP/2.0 100")) {
            Serial.println("SIP: Код ответа: 100 Trying");
        }
        else if (strstr(buffer, "SIP/2.0 180")) {
            Serial.println("SIP: Код ответа: 180 Ringing");
        }
        else if (strstr(buffer, "SIP/2.0 183")) {
            Serial.println("SIP: Код ответа: 183 Session Progress");
        }
        else {
            Serial.printf("SIP: Неизвестный код ответа, первые 50 символов: %.50s\n", buffer);
        }
        
        handleIncomingResponse(buffer, len, remote_ip, remotePort);
    }
    else if (strncmp(buffer, "INVITE ", 7) == 0) {
        Serial.println("SIP: Тип: INVITE (запрос)");
        handleIncomingINVITE(buffer, len, remote_ip, remotePort);
    }
    else if (strncmp(buffer, "BYE ", 4) == 0) {
        Serial.println("SIP: Тип: BYE (завершение вызова)");
        handleIncomingBYE(buffer, len, remote_ip, remotePort);
    }
    else if (strncmp(buffer, "ACK ", 4) == 0) {
        Serial.println("SIP: Тип: ACK (подтверждение)");
        Serial.println("SIP: === ВАЖНО: ПОЛУЧЕН ACK! ===");
        handleIncomingACK(buffer, len, remote_ip, remotePort);
    }
    else if (strncmp(buffer, "CANCEL ", 7) == 0) {
        Serial.println("SIP: Тип: CANCEL (отмена вызова)");
        handleIncomingCANCEL(buffer, len, remote_ip, remotePort);
    }
    else if (strncmp(buffer, "OPTIONS ", 8) == 0) {
        Serial.println("SIP: Тип: OPTIONS (опрос)");
        sendResponse(200, "OK", remote_ip, remotePort, buffer, nullptr, false, 0);
    }
    else if (strncmp(buffer, "REGISTER ", 9) == 0) {
        Serial.println("SIP: Тип: REGISTER (регистрация)");
        // Обычно клиент не должен получать REGISTER, но на всякий случай
        sendResponse(405, "Method Not Allowed", remote_ip, remotePort, buffer, nullptr, false, 0);
    }
    else {
        Serial.printf("SIP: Неизвестный тип сообщения от %s:%d\n", remote_ip, remotePort);
        Serial.printf("SIP: Начало сообщения: %.100s\n", buffer);
        
        // Попробуем определить по первому слову
        char first_word[32];
        sscanf(buffer, "%31s", first_word);
        Serial.printf("SIP: Первое слово: '%s'\n", first_word);
        
        sendResponse(501, "Not Implemented", remote_ip, remotePort, buffer, nullptr, false, 0);
    }
    
    Serial.println("==========================================\n");
}
void EnhancedSIPClient::setSIPCredentials(const char* user, const char* password, const char* server, uint16_t port) {
    strncpy(sip_user, user, sizeof(sip_user) - 1);
    sip_user[sizeof(sip_user) - 1] = '\0';

    strncpy(sip_password, password, sizeof(sip_password) - 1);
    sip_password[sizeof(sip_password) - 1] = '\0';

    strncpy(sip_server, server, sizeof(sip_server) - 1);
    sip_server[sizeof(sip_server) - 1] = '\0';

    sip_server_port = port;
    // Генерация уникального Call-ID для сессии
    snprintf(call_id, sizeof(call_id), "%lu@%s", esp_random(), sip_server);
    Serial.printf("SIP: Установлен Call-ID сессии: %s\n", call_id);
}

void EnhancedSIPClient::handleRegistration(bool is_retry_after_401) {
    if (!networkManager || !configManager) {
        Serial.println("SIP: handleRegistration - networkManager или configManager не инициализированы");
        return;
    }

    const char* local_ip = networkManager->getLocalIP();
    if (!local_ip || strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: handleRegistration - IP-адрес недоступен");
        return;
    }

    // --- Формирование базового REGISTER или с аутентификацией ---
   char register_msg[1024]; // Достаточно большой буфер
    const char* sip_domain = configManager->getSIPDomain();
    const char* register_target = (sip_domain && strlen(sip_domain) > 0) ? sip_domain : sip_server;
    if (!register_target || strlen(register_target) == 0) {
        Serial.println("SIP: handleRegistration - SIP Server/Domain не задан");
        return;
    }

    // Определяем, нужна ли аутентификация на основе флага и состояния
    bool add_auth = is_retry_after_401 && require_auth && strlen(auth_info.nonce) > 0;

   int len = snprintf(register_msg, sizeof(register_msg),
                   "REGISTER sip:%s SIP/2.0\r\n"
                   "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lu;rport\r\n"
                   "Max-Forwards: 70\r\n"           // <-- Перед From
                   "From: <sip:%s@%s>;tag=%lu\r\n"
                   "To: <sip:%s@%s>\r\n"
                   "Call-ID: %s\r\n"
                   "CSeq: %d REGISTER\r\n"
                   "User-Agent: Alina-IP-Phone/%s\r\n"  // <-- Перед Contact
                   "Contact: <sip:%s@%s:%d>\r\n"
                   "Expires: %d\r\n",               // <-- ВАЖНО: НЕТ \r\n\r\n и Content-Length в формате
                   // Аргументы:
                   register_target,
                   local_ip, SIP_PORT, esp_random(),
                   sip_user, register_target, esp_random(),
                   sip_user, register_target,
                   call_id,
                   sip_cseq++,
                   configManager->getDeviceName(), // <-- Аргумент для %s User-Agent
                   sip_user, local_ip, SIP_PORT,
                   register_expires); // <-- Последний аргумент

    if (len < 0 || len >= (int)sizeof(register_msg)) {
        Serial.println("SIP: handleRegistration - Ошибка формирования базового REGISTER: сообщение слишком длинное");
        return;
    }

    if (add_auth) {
        Serial.println("SIP: handleRegistration - Добавление Digest аутентификации (повторная попытка после 401)");

        // Используем realm из конфигурации, если задан, иначе из 401
        const char* realm_to_use = auth_info.realm; // Используем то, что распарсили из 401
        const char* config_realm = configManager->getSIPRealm();
        if (config_realm && strlen(config_realm) > 0) {
            realm_to_use = config_realm;
            Serial.printf("SIP: handleRegistration - Используется realm из конфигурации: %s\n", realm_to_use);
        }

        // Генерация cnonce
        char cnonce[33];
        snprintf(cnonce, sizeof(cnonce), "%08x", esp_random());

        // nc (Nonce Count) - всегда 1 для первого запроса с аутентификацией
        char nc[9] = "00000001";

        // qop - определяем из конфига
        bool qop_enabled = configManager->isQOPEnabled();
        const char* qop_str = qop_enabled ? "auth" : nullptr;

        // --- Формирование строки ответа Digest ---
        String ha1_input = String(sip_user) + ":" + String(realm_to_use) + ":" + String(sip_password);
        unsigned char ha1[16];
        mbedtls_md5_context ctx1;
        mbedtls_md5_init(&ctx1);
        // int ret = mbedtls_md5_starts_ret(&ctx1); // <-- Старая строка
        int ret = mbedtls_md5_starts(&ctx1); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка инициализации MD5 для HA1");
            mbedtls_md5_free(&ctx1);
            return; // Прервать выполнение
        }
        // ret = mbedtls_md5_update_ret(&ctx1, (const unsigned char*)ha1_input.c_str(), ha1_input.length()); // <-- Старая строка
        ret = mbedtls_md5_update(&ctx1, (const unsigned char*)ha1_input.c_str(), ha1_input.length()); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка обновления MD5 для HA1");
            mbedtls_md5_free(&ctx1);
            return;
        }
        // ret = mbedtls_md5_finish_ret(&ctx1, ha1); // <-- Старая строка
        ret = mbedtls_md5_finish(&ctx1, ha1); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка завершения MD5 для HA1");
            mbedtls_md5_free(&ctx1);
            return;
        }
        mbedtls_md5_free(&ctx1); // <-- Важно: освободить контекст

        char ha1_hex[33];
        for (int i = 0; i < 16; ++i) {
            sprintf(&ha1_hex[i * 2], "%02x", ha1[i]);
        }

        String uri_str = "sip:";
        if (sip_domain && strlen(sip_domain) > 0) {
            uri_str += sip_domain;
        } else {
            uri_str += sip_server;
        }
        String ha2_input = "REGISTER:" + uri_str;
        unsigned char ha2[16];
        mbedtls_md5_context ctx2;
        mbedtls_md5_init(&ctx2);
        // ret = mbedtls_md5_starts_ret(&ctx2); // <-- Старая строка
        ret = mbedtls_md5_starts(&ctx2); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка инициализации MD5 для HA2");
            mbedtls_md5_free(&ctx2);
            return;
        }
        // ret = mbedtls_md5_update_ret(&ctx2, (const unsigned char*)ha2_input.c_str(), ha2_input.length()); // <-- Старая строка
        ret = mbedtls_md5_update(&ctx2, (const unsigned char*)ha2_input.c_str(), ha2_input.length()); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка обновления MD5 для HA2");
            mbedtls_md5_free(&ctx2);
            return;
        }
        // ret = mbedtls_md5_finish_ret(&ctx2, ha2); // <-- Старая строка
        ret = mbedtls_md5_finish(&ctx2, ha2); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка завершения MD5 для HA2");
            mbedtls_md5_free(&ctx2);
            return;
        }
        mbedtls_md5_free(&ctx2); // <-- Важно: освободить контекст

        char ha2_hex[33];
        for (int i = 0; i < 16; ++i) {
            sprintf(&ha2_hex[i * 2], "%02x", ha2[i]);
        }

        String response_input;
        if (qop_enabled) {
            response_input = String(ha1_hex) + ":" + String(auth_info.nonce) + ":" +
                             String(nc) + ":" + String(cnonce) + ":" + String(qop_str) + ":" + String(ha2_hex);
        } else {
            response_input = String(ha1_hex) + ":" + String(auth_info.nonce) + ":" + String(ha2_hex);
        }

        unsigned char response[16];
        mbedtls_md5_context ctx3;
        mbedtls_md5_init(&ctx3);
        // ret = mbedtls_md5_starts_ret(&ctx3); // <-- Старая строка
        ret = mbedtls_md5_starts(&ctx3); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка инициализации MD5 для Response");
            mbedtls_md5_free(&ctx3);
            return;
        }
        // ret = mbedtls_md5_update_ret(&ctx3, (const unsigned char*)response_input.c_str(), response_input.length()); // <-- Старая строка
        ret = mbedtls_md5_update(&ctx3, (const unsigned char*)response_input.c_str(), response_input.length()); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка обновления MD5 для Response");
            mbedtls_md5_free(&ctx3);
            return;
        }
        // ret = mbedtls_md5_finish_ret(&ctx3, response); // <-- Старая строка
        ret = mbedtls_md5_finish(&ctx3, response); // <-- Изменено
        if (ret != 0) {
            Serial.println("SIP: handleRegistration - Ошибка завершения MD5 для Response");
            mbedtls_md5_free(&ctx3);
            return;
        }
        mbedtls_md5_free(&ctx3); // <-- Важно: освободить контекст

        char response_hex[33];
        for (int i = 0; i < 16; ++i) {
            sprintf(&response_hex[i * 2], "%02x", response[i]);
        }

        // --- Формирование заголовка Authorization ---
        char auth_header[512];
        int auth_len;
        if (qop_enabled) {
            auth_len = snprintf(auth_header, sizeof(auth_header),
                                "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", cnonce=\"%s\", nc=%s, qop=\"%s\", response=\"%s\", algorithm=MD5\r\n",
                                sip_user, realm_to_use, auth_info.nonce, uri_str.c_str(), cnonce, nc, qop_str, response_hex);
        } else {
            auth_len = snprintf(auth_header, sizeof(auth_header),
                                "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\", algorithm=MD5\r\n",
                                sip_user, realm_to_use, auth_info.nonce, uri_str.c_str(), response_hex);
        }

        if (auth_len < 0 || auth_len >= (int)sizeof(auth_header)) {
            Serial.println("SIP: handleRegistration - Ошибка формирования заголовка Authorization: слишком длинный");
            return;
        }

        // --- Объединение базового сообщения и заголовка Authorization ---
        if (len + auth_len + 2 + 20 >= (int)sizeof(register_msg)) {
             Serial.println("SIP: handleRegistration - Ошибка: Общее сообщение REGISTER с аутентификацией слишком длинное");
             return;
        }
        strcat(register_msg, auth_header);
        strcat(register_msg, "Content-Length: 0\r\n\r\n");
        Serial.println("SIP: handleRegistration - Сформирован REGISTER с Digest аутентификацией");
    } else {
        // Это первая попытка, без аутентификации
        strcat(register_msg, "Content-Length: 0\r\n\r\n");
        Serial.println("SIP: handleRegistration - Сформирован базовый REGISTER");
    }
    Serial.println(register_msg);
    // --- Отправка сообщения ---
    sendSIPMessage(sip_server, sip_server_port, register_msg);

    // Устанавливаем состояние РЕГИСТРИРУЕТСЯ, если это первая попытка
    if (!is_retry_after_401) {
        sip_state = SIP_STATE_REGISTERING;
    }
    // Если это повторная попытка, состояние уже было REGISTERING
}

// void EnhancedSIPClient::processIncomingPacket(AsyncUDPPacket packet) {
//     if (packet.length() == 0) return;

//     // Копируем данные пакета во временный буфер
//     char buffer[1500]; // Размер буфера для SIP-сообщения (обычно до 1500 байт)
//     size_t len = min((size_t)packet.length(), sizeof(buffer) - 1);
//     memcpy(buffer, packet.data(), len);
//     buffer[len] = '\0'; // Обязательно завершаем строку

//     // Определяем IP и порт отправителя
//     const char* remote_ip = packet.remoteIP().toString().c_str();
//     uint16_t remote_port = packet.remotePort();

//     Serial.printf("SIP: Получено SIP-сообщение от %s:%d\n", remote_ip, remote_port);
//     Serial.println(buffer);

//     // Проверяем, является ли это ответом (начинается с "SIP/2.0")
//     if (strncmp(buffer, "SIP/2.0", 7) == 0) {
//         handleIncomingResponse(buffer, len, remote_ip, remote_port);
//     }
//     // Проверяем, является ли это запросом (например, INVITE, BYE)
//     else if (strncmp(buffer, "INVITE ", 7) == 0) {
//         handleIncomingINVITE(buffer, len, remote_ip, remote_port);
//     }
//     else if (strncmp(buffer, "BYE ", 4) == 0) {
//         handleIncomingBYE(buffer, len, remote_ip, remote_port);
//     }
//     else if (strncmp(buffer, "ACK ", 4) == 0) {
//         handleIncomingACK(buffer, len, remote_ip, remote_port);
//     }
//     else if (strncmp(buffer, "CANCEL ", 7) == 0) {
//         handleIncomingCANCEL(buffer, len, remote_ip, remote_port);
//     }
//     else {
//         Serial.printf("SIP: Получен неизвестный тип SIP-сообщения от %s:%d\n", remote_ip, remote_port);
//         Serial.println(buffer);
//         // Ответить 405 Method Not Allowed или 501 Not Implemented
//         sendResponse(501, "Not Implemented", remote_ip, remote_port, buffer, nullptr, false, 0);
//     }
// }

// EnhancedSIPClient.cpp (внутри класса)

// EnhancedSIPClient.cpp (внутри класса)

void EnhancedSIPClient::handleIncomingResponse(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.printf("SIP: Обработка ответа от %s:%d\n", remote_ip, remote_port);

    // Проверка на 401 Unauthorized для REGISTER
    if (strstr(data, "401 Unauthorized") && strstr(data, "REGISTER")) {
        Serial.println("SIP: Получен 401 Unauthorized для REGISTER - требуется аутентификация");

        // Ищем заголовок WWW-Authenticate
        const char* auth_header = strstr(data, "WWW-Authenticate:");
        if (auth_header) {
            Serial.println("SIP: Найден WWW-Authenticate заголовок, парсим...");
            parseAuthHeader(auth_header, &auth_info);
            require_auth = true; // Отмечаем, что аутентификация требуется

            // Используем realm из конфигурации, если он задан
            const char* config_realm = configManager->getSIPRealm();
            if (config_realm && strlen(config_realm) > 0) {
                strncpy(auth_info.realm, config_realm, sizeof(auth_info.realm) - 1);
                auth_info.realm[sizeof(auth_info.realm) - 1] = '\0';
                Serial.printf("SIP: Используется realm из конфигурации: %s\n", auth_info.realm);
            }

            // НЕ вызываем handleRegistration(true) здесь!
            // Вместо этого, устанавливаем флаг, и пусть process() вызовет handleRegistration с нужным флагом.
            Serial.println("SIP: Информация об аутентификации сохранена. Ожидание вызова process() для повторной отправки.");

        } else {
            Serial.println("SIP: Ошибка - WWW-Authenticate заголовок не найден в 401 ответе");
        }
        return; // Выйти после обработки 401
    }

    // Проверка на 200 OK для REGISTER
    if (strncmp(data, "SIP/2.0 200 OK", 14) == 0) {
        if (strstr(data, "REGISTER")) {
            Serial.println("SIP: Успешная регистрация на SIP сервере!");
            sip_registered = true;
            last_register_success = millis();
            sip_state = SIP_STATE_REGISTERED;
            require_auth = false; // СБРОС аутентификации после успеха
            if (audioManager) {
                audioManager->startTasks();
                Serial.println("SIP: AudioManager задачи запущены после успешной регистрации");
            } else {
                Serial.println("SIP: ВНИМАНИЕ - audioManager не доступен для запуска задач");
            }
            // Извлечение Expires из ответа (опционально)
            char expires_str[16];
            if (extractSIPHeader(data, len, "Expires:", expires_str, sizeof(expires_str))) {
                int expires = atoi(expires_str);
                if (expires > 0) {
                    register_expires = expires;
                    Serial.printf("SIP: Получен Expires: %d секунд\n", register_expires);
                }
            }

            // Сбросить информацию об аутентификации после успешной регистрации
            memset(&auth_info, 0, sizeof(auth_info));
            return;
        }
    }

    // Проверка на 486 Busy Here или 603 Decline для INVITE
    if ((strstr(data, "486 Busy Here") || strstr(data, "603 Decline")) && strstr(data, "INVITE")) {
        char cseq_str[16];
        if (extractSIPHeader(data, len, "CSeq:", cseq_str, sizeof(cseq_str))) {
            int cseq_num = atoi(cseq_str);
            call_t* call = nullptr;
            for (int i = 0; i < max_calls; i++) {
                if (calls[i].state != CALL_STATE_IDLE && calls[i].cseq_invite == cseq_num) {
                    call = &calls[i];
                    break;
                }
            }
            if (call) {
                Serial.printf("SIP: Вызов %d отклонен (%s)\n", call->id, strstr(data, "486") ? "Busy Here" : "Decline");
                // Отправить BYE, если сервер ожидает
                // sendBYE(call); // Опционально
                resetCall(call);
                active_calls = max(0, active_calls - 1);
            } else {
                Serial.println("SIP: Ошибка: Не найден вызов для 486/603 INVITE");
            }
        }
        return;
    }

    // Проверка на 404 Not Found для INVITE (например, номер не существует)
    if (strstr(data, "404 Not Found") && strstr(data, "INVITE")) {
        char cseq_str[16];
        if (extractSIPHeader(data, len, "CSeq:", cseq_str, sizeof(cseq_str))) {
            int cseq_num = atoi(cseq_str);
            call_t* call = nullptr;
            for (int i = 0; i < max_calls; i++) {
                if (calls[i].state != CALL_STATE_IDLE && calls[i].cseq_invite == cseq_num) {
                    call = &calls[i];
                    break;
                }
            }
            if (call) {
                Serial.printf("SIP: Вызов %d - номер не найден (404)\n", call->id);
                resetCall(call);
                active_calls = max(0, active_calls - 1);
            } else {
                Serial.println("SIP: Ошибка: Не найден вызов для 404 INVITE");
            }
        }
        return;
    }

    // Проверка на 487 Request Terminated для INVITE (ответ на CANCEL)
    if (strstr(data, "487 Request Terminated") && strstr(data, "INVITE")) {
        char cseq_str[16];
        if (extractSIPHeader(data, len, "CSeq:", cseq_str, sizeof(cseq_str))) {
            int cseq_num = atoi(cseq_str);
            call_t* call = nullptr;
            for (int i = 0; i < max_calls; i++) {
                if (calls[i].state != CALL_STATE_IDLE && calls[i].cseq_invite == cseq_num) {
                    call = &calls[i];
                    break;
                }
            }
            if (call) {
                Serial.printf("SIP: Вызов %d прерван (487)\n", call->id);
                resetCall(call);
                active_calls = max(0, active_calls - 1);
            } else {
                Serial.println("SIP: Ошибка: Не найден вызов для 487 INVITE");
            }
        }
        return;
    }

    // Проверка на 200 OK для BYE
    if (strncmp(data, "SIP/2.0 200 OK", 14) == 0) {
        if (strstr(data, "BYE")) {
            char call_id_str[CALL_ID_LEN];
            if (extractSIPHeader(data, len, "Call-ID:", call_id_str, sizeof(call_id_str))) {
                call_t* call = nullptr;
                for (int i = 0; i < max_calls; i++) {
                    if (calls[i].state != CALL_STATE_IDLE && strcmp(calls[i].call_id, call_id_str) == 0) {
                        call = &calls[i];
                        break;
                    }
                }
                if (call) {
                    Serial.printf("SIP: Получен 200 OK для BYE вызова %d\n", call->id);
                    resetCall(call);
                    active_calls = max(0, active_calls - 1);
                } else {
                    Serial.println("SIP: Ошибка: Не найден вызов для 200 OK BYE");
                }
            }
            return;
        }
    }

    // Обработка 200 OK для INVITE (отвеченный вызов)
    if (strncmp(data, "SIP/2.0 200 OK", 14) == 0) {
        if (strstr(data, "INVITE")) {
             handle200OK(data, len, remote_ip, remote_port);
             return;
        }
    }

    Serial.println("SIP: Получен неизвестный или неподдерживаемый SIP-ответ");
    Serial.println(data);
}


void EnhancedSIPClient::handleIncomingINVITE(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.println("SIP DEBUG: handleIncomingINVITE called.");
    
    // +++ ПРОВЕРКА НА ПОВТОРНЫЙ INVITE (РЕТРАНСЛЯЦИЯ) +++
    char incoming_call_id[CALL_ID_LEN];
    if (extractSIPHeader(data, len, "Call-ID:", incoming_call_id, sizeof(incoming_call_id))) {
        for (int i = 0; i < max_calls; i++) {
            if (calls[i].state != CALL_STATE_IDLE && strcmp(calls[i].call_id, incoming_call_id) == 0) {
                Serial.printf("SIP: Получен повторный INVITE для существующего вызова %d (Call-ID: %s)\n", i, incoming_call_id);
                Serial.printf("SIP: Текущее состояние вызова: %d\n", calls[i].state);
                
                // Если вызов в состоянии WAITING_FOR_ACK, повторно отправим 200 OK с ТЕМ ЖЕ To-tag
                if (calls[i].state == CALL_STATE_WAITING_FOR_ACK) {
                    Serial.println("SIP: Повторная отправка 200 OK для ретранслированного INVITE");
                    
                    // Обновляем активность вызова
                    calls[i].last_activity = millis();
                    
                    // КРИТИЧЕСКИ ВАЖНО: используем СУЩЕСТВУЮЩИЙ To-tag, не генерируем новый!
                    sendResponse(200, "OK", calls[i].remote_ip, calls[i].remote_sip_port, 
                                data, calls[i].to_tag, true, calls[i].local_rtp_port);
                    
                    Serial.println("SIP: 200 OK отправлен повторно для ретрансляции");
                } else if (calls[i].state == CALL_STATE_ACTIVE) {
                    Serial.println("SIP: Вызов уже активен, игнорируем ретрансляцию INVITE");
                }
                return; // Выходим, не создавая новый вызов
            }
        }
    }
    // +++++++++++++++++++++++++++++++++++++++++++++++++++++++

    int slot = findFreeCallSlot();
    if (slot < 0) {
        Serial.println("SIP: Ошибка: Нет свободных слотов для входящего вызова");
        sendResponse(503, "Service Unavailable", remote_ip, remote_port, data, nullptr, false, 0);
        return;
    }
    
    Serial.printf("SIP DEBUG: Found free call slot: %d\n", slot);
    call_t* call = &calls[slot];
    Serial.printf("SIP DEBUG: call pointer address: 0x%p\n", call);
    resetCall(call);
    Serial.println("SIP DEBUG: resetCall executed.");

    // --- ИЗВЛЕЧЕНИЕ ЗАГОЛОВКОВ ---
    Serial.println("SIP DEBUG: Starting header extraction...");
    
    if (!extractSIPHeader(data, len, "Call-ID:", call->call_id, sizeof(call->call_id))) {
         Serial.println("SIP: Ошибка извлечения Call-ID");
         sendResponse(400, "Bad Request", remote_ip, remote_port, data, nullptr, false, 0);
         resetCall(call);
         return;
    }
    if (!extractSIPHeader(data, len, "From:", call->from_uri, sizeof(call->from_uri))) {
         Serial.println("SIP: Ошибка извлечения From URI");
         sendResponse(400, "Bad Request", remote_ip, remote_port, data, nullptr, false, 0);
         resetCall(call);
         return;
    }
    if (!extractSIPHeader(data, len, "To:", call->to_uri, sizeof(call->to_uri))) {
         Serial.println("SIP: Ошибка извлечения To URI");
         sendResponse(400, "Bad Request", remote_ip, remote_port, data, nullptr, false, 0);
         resetCall(call);
         return;
    }

    // Извлечение CSeq числа и сохранение в call->cseq_invite
    char cseq_header_str[32];
    if (!extractSIPHeader(data, len, "CSeq:", cseq_header_str, sizeof(cseq_header_str))) {
         Serial.println("SIP: Ошибка извлечения CSeq");
         sendResponse(400, "Bad Request", remote_ip, remote_port, data, nullptr, false, 0);
         resetCall(call);
         return;
    } else {
        char* end_ptr;
        long cseq_num = strtol(cseq_header_str, &end_ptr, 10);
        if (cseq_num <= 0 || cseq_num > 0x7FFFFFFF) {
            Serial.println("SIP: Ошибка: Неверное значение CSeq");
            sendResponse(400, "Bad Request", remote_ip, remote_port, data, nullptr, false, 0);
            resetCall(call);
            return;
        }
        call->cseq_invite = (uint32_t)cseq_num;
        Serial.printf("SIP DEBUG: CSeq INVITE extracted and saved: %u\n", call->cseq_invite);
    }

    if (!extractSIPHeader(data, len, "Contact:", call->contact_uri, sizeof(call->contact_uri))) {
            Serial.println("SIP: Ошибка извлечения Contact URI");
            // Используем SIP сервер как резерв
            snprintf(call->contact_uri, sizeof(call->contact_uri), "sip:%s@%s:%d", 
                    configManager->getSIPUsername(), sip_server, sip_server_port);
            call->contact_uri[sizeof(call->contact_uri) - 1] = '\0';
    }

    // Извлечение From Tag
    const char* tag_start = strstr(call->from_uri, ";tag=");
    if (tag_start) {
        tag_start += 5;
        const char* tag_end = strchr(tag_start, '"');
        if (!tag_end) tag_end = strchr(tag_start, ';');
        if (!tag_end) tag_end = strchr(tag_start, '>');
        if (!tag_end) tag_end = call->from_uri + strlen(call->from_uri);

        int tag_len = tag_end - tag_start;
        if (tag_len < sizeof(call->from_tag)) {
            strncpy(call->from_tag, tag_start, tag_len);
            call->from_tag[tag_len] = '\0';
        } else {
            Serial.println("SIP: Warning: From tag too long");
            strncpy(call->from_tag, tag_start, sizeof(call->from_tag) - 1);
            call->from_tag[sizeof(call->from_tag) - 1] = '\0';
        }
    }

    if (!extractSIPHeader(data, len, "Record-Route:", call->record_route, sizeof(call->record_route))) {
        call->record_route[0] = '\0';
    }

    // --- ИЗВЛЕЧЕНИЕ SDP (для remote_rtp_ip и remote_rtp_port) ---
    const char* sdp_start = strstr(data, "\r\n\r\n");
    char temp_remote_rtp_ip[16] = {0};
    uint16_t temp_remote_rtp_port = 0;

    if (sdp_start) {
        sdp_start += 4;
        Serial.printf("SIP DEBUG: SDP Body:\n%s\n", sdp_start);

        // Извлечение IP из строки c=IN IP4 ...
        const char* c_line = strstr(sdp_start, "c=IN IP4 ");
        if (c_line) {
            c_line += 9;
            const char* c_end = strchr(c_line, '\n');
            if (c_end) {
                int ip_len = c_end - c_line;
                if (c_line[ip_len - 1] == '\r') ip_len--;
                if (ip_len < sizeof(temp_remote_rtp_ip)) {
                    strncpy(temp_remote_rtp_ip, c_line, ip_len);
                    temp_remote_rtp_ip[ip_len] = '\0';
                    Serial.printf("SIP: Извлечен remote_rtp_ip из SDP: %s\n", temp_remote_rtp_ip);
                } else {
                    Serial.println("SIP: Warning: remote_rtp_ip from SDP is too long");
                    strncpy(temp_remote_rtp_ip, c_line, sizeof(temp_remote_rtp_ip) - 1);
                    temp_remote_rtp_ip[sizeof(temp_remote_rtp_ip) - 1] = '\0';
                }
            } else {
                 Serial.println("SIP: Warning: Could not extract IP from c= line in SDP");
                 strncpy(temp_remote_rtp_ip, remote_ip, sizeof(temp_remote_rtp_ip) - 1);
                 temp_remote_rtp_ip[sizeof(temp_remote_rtp_ip) - 1] = '\0';
            }
        } else {
            Serial.println("SIP: Warning: c=IN IP4 line not found in SDP, using INVITE remote_ip");
            strncpy(temp_remote_rtp_ip, remote_ip, sizeof(temp_remote_rtp_ip) - 1);
            temp_remote_rtp_ip[sizeof(temp_remote_rtp_ip) - 1] = '\0';
        }

        // Парсинг порта из строки m=audio
        const char* m_line = strstr(sdp_start, "m=audio ");
        if (m_line) {
            m_line += 8;
            while (*m_line && (*m_line == ' ' || *m_line == '\t')) m_line++;
            if (*m_line) {
                const char* port_start = m_line;
                while (*m_line && *m_line != ' ' && *m_line != '\t' && *m_line != '\r' && *m_line != '\n') m_line++;
                int port_len = m_line - port_start;
                if (port_len > 0 && port_len <= 5) {
                    char port_str[6];
                    strncpy(port_str, port_start, port_len);
                    port_str[port_len] = '\0';
                    temp_remote_rtp_port = atoi(port_str);
                    Serial.printf("SIP: Извлечен remote_rtp_port из SDP: %d\n", temp_remote_rtp_port);
                } else {
                    Serial.println("SIP: Warning: remote_rtp_port from SDP is invalid or too long, using default");
                    temp_remote_rtp_port = 4008;
                }
            } else {
                Serial.println("SIP: Warning: No content after 'm=audio ' in SDP, using default port");
                temp_remote_rtp_port = 4008;
            }
        } else {
            Serial.println("SIP: Warning: m=audio line not found in SDP, using default port");
            temp_remote_rtp_port = 4008;
        }
    } else {
        Serial.println("SIP: Warning: SDP body not found in INVITE");
        strncpy(temp_remote_rtp_ip, remote_ip, sizeof(temp_remote_rtp_ip) - 1);
        temp_remote_rtp_ip[sizeof(temp_remote_rtp_ip) - 1] = '\0';
        temp_remote_rtp_port = 4008;
    }

    Serial.println("SIP DEBUG: Finished extracting headers and SDP");

    // --- НАЗНАЧЕНИЕ ЛОКАЛЬНОГО RTP ПОРТА и SSRC ---
    call->local_rtp_port = configManager->getRTPBasePort() + (slot * 2);
    call->ssrc = esp_random();
    Serial.printf("SIP DEBUG: Assigned local RTP port: %d, SSRC: %u\n", call->local_rtp_port, call->ssrc);

    // --- НАСТРОЙКА RTP КАНАЛА ---
    uint8_t payload_type = 8;
    if (!rtpManager->setupChannel(slot, temp_remote_rtp_ip, temp_remote_rtp_port, call->local_rtp_port, call->ssrc, payload_type)) {
        Serial.printf("SIP: Ошибка: Не удалось настроить RTP канал %d\n", slot);
        sendResponse(500, "Internal Server Error", remote_ip, remote_port, data, nullptr, false, 0);
        resetCall(call);
        return;
    }
    Serial.printf("SIP DEBUG: RTP channel %d setup completed.\n", slot);

    // --- ОПРЕДЕЛЕНИЕ АДРЕСА ОТПРАВКИ ОТВЕТА (200 OK) ---
    char target_ip[16] = {0};
    uint16_t target_port = 5060;

    // Попробуем извлечь IP и порт из Contact URI
    if (strlen(call->contact_uri) > 0) {
        const char* start_ip = strstr(call->contact_uri, "@");
        if (start_ip) {
            start_ip++;
            const char* end_ip = strchr(start_ip, ':');
            const char* end_port = strchr(start_ip, ';');
            const char* end_bracket = strchr(start_ip, '>');

            if (end_ip) {
                int ip_len = end_ip - start_ip;
                if (ip_len < sizeof(target_ip)) {
                    strncpy(target_ip, start_ip, ip_len);
                    target_ip[ip_len] = '\0';

                    const char* port_start = end_ip + 1;
                    const char* port_end = end_port ? end_port : (end_bracket ? end_bracket : nullptr);
                    if (port_end) {
                        int port_len = port_end - port_start;
                        char port_str[8];
                        if (port_len < sizeof(port_str)) {
                            strncpy(port_str, port_start, port_len);
                            port_str[port_len] = '\0';
                            target_port = atoi(port_str);
                        } else {
                            Serial.println("SIP: Warning: Port string too long in Contact URI");
                        }
                    } else {
                        target_port = 5060;
                    }
                } else {
                    Serial.println("SIP: Warning: IP string too long in Contact URI");
                }
            } else {
                 Serial.println("SIP: Warning: Could not parse IP from Contact URI");
            }
        } else {
             Serial.println("SIP: Warning: Could not find '@' in Contact URI");
        }
    }

    // Если парсинг Contact URI не удался, используем remote_ip из пакета
    if (strlen(target_ip) == 0 || strcmp(target_ip, "0.0.0.0") == 0) {
        if (strlen(remote_ip) > 0 && strcmp(remote_ip, "0.0.0.0") != 0) {
            Serial.printf("SIP: Using packet remote_ip %s as target\n", remote_ip);
            strcpy(target_ip, remote_ip);
            target_port = remote_port;
        } else {
            Serial.printf("SIP: Using SIP server %s as target\n", sip_server);
            strcpy(target_ip, sip_server);
            target_port = sip_server_port;
        }
    }

    Serial.printf("SIP DEBUG: Determined target for 200 OK: IP=%s, Port=%d\n", target_ip, target_port);

    // --- СОХРАНЕНИЕ ИНФОРМАЦИИ В СТРУКТУРУ ВЫЗОВА ---
    strncpy(call->remote_ip, target_ip, sizeof(call->remote_ip) - 1);
    call->remote_ip[sizeof(call->remote_ip) - 1] = '\0';
    call->remote_sip_port = target_port;

    // +++ КРИТИЧЕСКИ ВАЖНЫЙ КОД: ГЕНЕРАЦИЯ To-tag ДО ОТПРАВКИ ОТВЕТОВ +++
    char initial_to_tag[TAG_LEN] = {0};
    snprintf(initial_to_tag, sizeof(initial_to_tag), "%lu", esp_random());
    
    // Сохраняем To-tag в структуре вызова для использования при ретрансляции
    strncpy(call->to_tag, initial_to_tag, sizeof(call->to_tag) - 1);
    call->to_tag[sizeof(call->to_tag) - 1] = '\0';
    
    Serial.printf("SIP: Сгенерирован To-tag для вызова %d: %s\n", call->id, initial_to_tag);
    // +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

    // --- ВЫЗОВ В ИСТОРИЮ ---
    if (webInterface) {
        Serial.printf("SIP DEBUG: Before addCallToHistory - webInterface ptr: 0x%p, call->from_uri: %s\n", (void*)webInterface, call->from_uri);
        webInterface->addCallToHistory(call->from_uri, "incoming", call->id);
        Serial.println("SIP DEBUG: addCallToHistory completed.");
    } else {
        Serial.println("SIP: ОШИБКА: webInterface не инициализирован при попытке добавить вызов в историю.");
    }

    // --- УСТАНОВКА СОСТОЯНИЯ ВЫЗОВА ---
    call->state = CALL_STATE_TRYING;
    call->last_activity = millis();
    Serial.println("Устанавливаем CALL_STATE_TRYING");

    // --- ОТПРАВКА 100 TRYING ---
    Serial.println("Отправка TRYING");
    sendTrying(data, target_ip, target_port);

    // --- ОТПРАВКА 180 RINGING ---
    Serial.println("Отправка RINGING");
    call->state = CALL_STATE_RINGING;
    call->last_activity = millis();
    sendRinging(data, target_ip, target_port, initial_to_tag); // Передаем To-tag

    // --- ОТПРАВКА 200 OK ---
    Serial.println("ОТПРАВКА 200 OK");
    // Используем тот же To-tag, что и в Ringing!
    sendResponse(200, "OK", target_ip, target_port, data, initial_to_tag, true, call->local_rtp_port);

    // Устанавливаем состояние ОЖИДАНИЯ ACK
    call->state = CALL_STATE_WAITING_FOR_ACK;
    call->last_activity = millis();
    Serial.printf("SIP: Вызов %d переведен в состояние WAITING_FOR_ACK (ожидание ACK)\n", call->id);

    Serial.printf("SIP: Получен входящий вызов %d от %s\n", call->id, call->from_uri);
    Serial.printf("SIP DEBUG: handleIncomingINVITE finished for call ID %d.\n", call->id);
}

void EnhancedSIPClient::handleIncomingBYE(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.println("SIP: Обработка входящего BYE");
    // Найти вызов по Call-ID
    char call_id[CALL_ID_LEN];
    if (!extractSIPHeader(data, len, "Call-ID:", call_id, sizeof(call_id))) {
        Serial.println("SIP: BYE без Call-ID");
        return;
    }

    call_t* call = nullptr;
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state != CALL_STATE_IDLE && strcmp(calls[i].call_id, call_id) == 0) {
            call = &calls[i];
            break;
        }
    }

    if (!call) {
        Serial.println("SIP: BYE для несуществующего вызова");
        // Ответить 481 Call/Transaction Does Not Exist
        sendResponse(481, "Call/Transaction Does Not Exist", remote_ip, remote_port, data, nullptr, false, 0);
        return;
    }

    Serial.printf("SIP: Получен BYE для вызова %d\n", call->id);

    // Ответить 200 OK на BYE
    sendResponse(200, "OK", call->remote_ip, call->remote_sip_port, data, nullptr, false, 0);

    // Сбросить вызов
    resetCall(call);
    active_calls = max(0, active_calls - 1);
}

void EnhancedSIPClient::handleIncomingACK(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.println("SIP: Обработка входящего ACK");
    // Найти вызов по Call-ID
    char call_id[CALL_ID_LEN];
    if (!extractSIPHeader(data, len, "Call-ID:", call_id, sizeof(call_id))) {
        Serial.println("SIP: ACK без Call-ID");
        return;
    }

    call_t* call = nullptr;
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state != CALL_STATE_IDLE && strcmp(calls[i].call_id, call_id) == 0) {
            call = &calls[i];
            break;
        }
    }

    if (!call) {
        Serial.println("SIP: ACK для несуществующего вызова");
        return;
    }

    if (call->state == CALL_STATE_WAITING_FOR_ACK) {
        Serial.printf("SIP: Получен ACK для входящего вызова %d\n", call->id);
        // Переход в состояние разговора
        call->state = CALL_STATE_ACTIVE;
        call->last_activity = millis();
        
        // Запуск RTP (если нужно)
        
        // audioManager.startStream(call->local_rtp_port, call->remote_ip, call->remote_rtp_port, call->ssrc);
    } else {
        Serial.printf("SIP: Получен ACK для вызова %d в состоянии %d (ожидалось WAITING_FOR_ACK)\n", call->id, call->state);
    }
}

void EnhancedSIPClient::handleIncomingCANCEL(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.println("SIP: Обработка входящего CANCEL");
    // Найти вызов по Call-ID
    char call_id[CALL_ID_LEN];
    if (!extractSIPHeader(data, len, "Call-ID:", call_id, sizeof(call_id))) {
        Serial.println("SIP: CANCEL без Call-ID");
        return;
    }

    call_t* call = nullptr;
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state != CALL_STATE_IDLE && strcmp(calls[i].call_id, call_id) == 0) {
            call = &calls[i];
            break;
        }
    }

    if (!call) {
        Serial.println("SIP: CANCEL для несуществующего вызова");
        // Ответить 481 Call/Transaction Does Not Exist
        sendResponse(481, "Call/Transaction Does Not Exist", remote_ip, remote_port, data, nullptr, false, 0);
        return;
    }

    Serial.printf("SIP: Получен CANCEL для вызова %d (состояние: %d)\n", call->id, call->state);

    // Ответить 200 OK на CANCEL
    sendResponse(200, "OK", remote_ip, remote_port, data, nullptr, false, 0);

    // Если вызов в состоянии RINGING или WAITING_FOR_ACK, отправить 487 Request Terminated
    if (call->state == CALL_STATE_RINGING || call->state == CALL_STATE_WAITING_FOR_ACK) {
        // Извлечение CSeq из CANCEL
        char cseq_str[16];
        extractSIPHeader(data, len, "CSeq:", cseq_str, sizeof(cseq_str));
        int cancel_cseq = atoi(cseq_str);
        // Ответить 487 на оригинальный INVITE
        // Для этого нужно отправить 487 с CSeq оригинального INVITE
        char response_487[1024];
        const char* local_ip = networkManager->getLocalIP();
        // ВАЖНО: Проверяем, что IP не 0.0.0.0 перед отправкой
        if (strcmp(local_ip, "0.0.0.0") == 0) {
            Serial.println("SIP: Ошибка: Локальный IP 0.0.0.0, невозможно отправить 487");
            return; // Пропускаем отправку
        }
        uint32_t branch = esp_random();
        snprintf(response_487, sizeof(response_487),
                 "SIP/2.0 487 Request Terminated\r\n"
                 "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lu\r\n"
                 "From: <sip:%s@%s>;tag=%s\r\n"
                 "To: <sip:%s@%s>;tag=%s\r\n"
                 "Call-ID: %s\r\n"
                 "CSeq: %d INVITE\r\n" // Используем CSeq оригинального INVITE
                 "User-Agent: ALINA/1.0\r\n"
                 "Content-Length: 0\r\n\r\n",
                 local_ip, SIP_PORT, branch, // <-- Вот тут будет правильный IP
                 sip_user, sip_server, call->from_tag, // Предполагаем, что from_tag был установлен
                 sip_user, sip_server, call->to_tag,   // to_tag для входящего вызова
                 call->call_id,
                 call->cseq_invite); // Оригинальный CSeq INVITE

        sendSIPMessage(call->remote_ip, call->remote_sip_port, response_487);
    }

    // Сбросить вызов
    resetCall(call);
    active_calls = max(0, active_calls - 1);
}


// EnhancedSIPClient.cpp (внутри класса)

void EnhancedSIPClient::makeCall(const char* to_uri) {
    if (!networkManager || !networkManager->isConnected()) {
        Serial.println("SIP: makeCall: Сеть не подключена\n");
        return;
    }
    if (!sip_registered) {
        Serial.println("SIP: Ошибка: SIP клиент не зарегистрирован\n");
        return;
    }
    int slot = findFreeCallSlot();
    if (slot < 0) {
        Serial.println("SIP: Ошибка: Нет свободных слотов для вызова\n");
        return;
    }

    call_t* call = &calls[slot];
    resetCall(call);
    call->id = slot;
    call->state = CALL_STATE_OUTGOING; // <-- Изменено
    strncpy(call->call_id, call_id, sizeof(call->call_id) - 1);
    call->call_id[sizeof(call->call_id) - 1] = '\0';
    strncpy(call->to_uri, to_uri, sizeof(call->to_uri) - 1);
    call->to_uri[sizeof(call->to_uri) - 1] = '\0';
    strncpy(call->from_uri, ("sip:" + String(sip_user) + "@" + String(sip_server)).c_str(), sizeof(call->from_uri) - 1);
    call->from_uri[sizeof(call->from_uri) - 1] = '\0';
    call->cseq_invite = sip_cseq++; // Увеличиваем общий CSeq для INVITE
    call->last_activity = millis();

    // Извлечение IP и порта из to_uri (предполагается формат sip:user@ip:port)
    parseContactURI(to_uri, call->remote_ip, &call->remote_sip_port);

    // Генерация локального RTP порта (проверка на занятость опциональна)
    call->local_rtp_port = configManager->getRTPBasePort() + (slot * 2);
    call->ssrc = esp_random(); // Генерация SSRC для RTP

    // Формирование INVITE сообщения
    char invite[1024]; // Увеличенный буфер
    const char* local_ip = getLocalIP(); // Используем публичный метод
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: Ошибка: Локальный IP 0.0.0.0, невозможно отправить INVITE\n");
        resetCall(call);
        return;
    }

    uint32_t branch = esp_random();
    int len = snprintf(invite, sizeof(invite),
                       "INVITE %s SIP/2.0\r\n"
                       "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lu;rport\r\n"
                       "From: %s;tag=%lu\r\n"
                       "To: %s\r\n"
                       "Call-ID: %s\r\n"
                       "CSeq: %d INVITE\r\n"
                       "Contact: <sip:%s@%s:%d>\r\n"
                       "Content-Type: application/sdp\r\n"
                       "Content-Length: %d\r\n\r\n"
                       "v=0\r\n"
                       "o=- %lu %lu IN IP4 %s\r\n"
                       "s=SIP Call\r\n"
                       "c=IN IP4 %s\r\n"
                       "t=0 0\r\n"
                       "m=audio %d RTP/AVP 0 8 9 101\r\n"
                       "a=rtpmap:0 PCMU/8000\r\n"
                       "a=rtpmap:8 PCMA/8000\r\n"
                       "a=rtpmap:9 G722/8000\r\n"
                       "a=rtpmap:101 telephone-event/8000\r\n"
                       "a=fmtp:101 0-15\r\n",
                       to_uri, // Request-URI
                       local_ip, SIP_PORT, branch, // <-- Вот тут будет правильный IP
                       call->from_uri, esp_random(), // From URI, tag
                       to_uri, // To URI
                       call->call_id, // Call-ID
                       call->cseq_invite, // CSeq
                       sip_user, local_ip, SIP_PORT, // Contact
                       120, // Примерная длина SDP, рассчитывается точно
                       esp_random(), esp_random(), local_ip, // o= line
                       local_ip, // c= line
                       call->local_rtp_port); // m= line port

    if (len < 0 || len >= (int)sizeof(invite)) {
        Serial.println("SIP: Ошибка: INVITE сообщение слишком длинное\n");
        resetCall(call);
        return;
    }

    Serial.printf("SIP: Отправляем INVITE:\n%s\n", invite);
    sendSIPMessage(call->remote_ip, call->remote_sip_port, invite);
    active_calls++;
    webInterface->addCallToHistory(to_uri, "outgoing", 0); // Добавляем в историю
}

void EnhancedSIPClient::hangupCall(int call_id) {
    if (call_id < 0 || call_id >= max_calls || calls[call_id].state == CALL_STATE_IDLE) {
        Serial.printf("SIP: Попытка завершить несуществующий вызов %d\n", call_id);
        return;
    }

    call_t* call = &calls[call_id];
    Serial.printf("SIP: Завершение вызова %d (состояние: %d)\n", call_id, call->state);

    if (call->state == CALL_STATE_ACTIVE || call->state == CALL_STATE_RINGING || call->state == CALL_STATE_WAITING_FOR_ACK) {
        // Формирование BYE сообщения
        char msg[512];
        const char* local_ip = networkManager->getLocalIP();
        // ВАЖНО: Проверяем, что IP не 0.0.0.0 перед отправкой
        if (strcmp(local_ip, "0.0.0.0") == 0) {
            Serial.println("SIP: Ошибка: Локальный IP 0.0.0.0, невозможно отправить BYE");
            // Все равно сбрасываем вызов
            resetCall(call);
            active_calls = max(0, active_calls - 1);
            return; // Пропускаем отправку
        }
        uint32_t branch = esp_random();
        // Используем CSeq для BYE (обычно увеличиваем CSeq INVITE на 1)
        uint32_t bye_cseq = call->cseq_invite + 1;

        int len = snprintf(msg, sizeof(msg),
                           "BYE %s SIP/2.0\r\n"
                           "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lu;rport\r\n"
                           "From: <sip:%s@%s>;tag=%s\r\n"
                           "To: <sip:%s@%s>;tag=%s\r\n" // Важно: использовать To-tag, если он был
                           "Call-ID: %s\r\n"
                           "CSeq: %lu BYE\r\n"
                           "User-Agent: ALINA/1.0\r\n"
                           "Content-Length: 0\r\n\r\n",
                           call->contact_uri, // Используем Contact URI из INVITE/200 OK, если доступно
                           local_ip, SIP_PORT, branch, // <-- Вот тут будет правильный IP
                           sip_user, sip_server, call->from_tag,
                           sip_user, sip_server, call->to_tag, // Убедитесь, что to_tag установлен
                           call->call_id,
                           bye_cseq);

        if (len > 0 && len < (int)sizeof(msg)) {
            Serial.printf("SIP: Отправляем BYE:\n%s", msg);
            // Отправляем BYE на IP и порт, указанные в Contact URI вызова или на IP отправителя INVITE
            // Если есть Record-Route, нужно отправить через прокси
            const char* target_ip = call->remote_ip; // По умолчанию
            uint16_t target_port = call->remote_sip_port;
            if (strlen(call->contact_uri) > 0) {
                 // Если есть Contact URI, парсим его для определения конечного адресата BYE
                 // Это более правильно, чем использовать IP отправителя INVITE, если вызов был через прокси
                 char contact_ip[16];
                 uint16_t contact_port;
                 parseContactURI(call->contact_uri, contact_ip, &contact_port);
                 if (strlen(contact_ip) > 0) {
                     target_ip = contact_ip;
                     target_port = contact_port;
                 }
                 Serial.printf("SIP: Отправляем BYE на %s:%d (из Contact URI)\n", target_ip, target_port);
            }
            sendSIPMessage(target_ip, target_port, msg);
        } else {
            Serial.println("SIP: Ошибка: BYE сообщение слишком длинное");
        }
    }

    // Сброс вызова
    resetCall(call);
    active_calls = max(0, active_calls - 1);
}


// --- ВСПОМОГАТЕЛЬНЫЕ МЕТОДЫ ---

// --- ОТПРАВКА ОТВЕТА НА ЗАПРОС ---
// --- ОТПРАВКА ОТВЕТА НА ЗАПРОС ---
// --- ОТПРАВКА ОТВЕТА НА ЗАПРОС ---
void EnhancedSIPClient::sendResponse(int code, const char* reason, const char* dst_ip, uint16_t dst_port,
                                     const char* request, const char* to_tag, bool with_sdp, uint16_t local_rtp_port) {
    
    Serial.printf("=== sendResponse ENTER === code: %d\n", code);
    Serial.printf("Stack free: %d\n", esp_get_free_heap_size());

    // --- ПРЕДВАРИТЕЛЬНЫЕ ПРОВЕРКИ ---
    if (!networkManager) {
        Serial.println("ERROR: networkManager is NULL");
        return;
    }
    if (!networkManager->isConnected()) {
        Serial.println("ERROR: Network not connected");
        return;
    }

    const char* local_ip = networkManager->getLocalIP();
    Serial.printf("Local IP: %s\n", local_ip);
    if (!local_ip || strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("ERROR: Invalid local IP");
        return;
    }

    if (!configManager) {
        Serial.println("ERROR: configManager is NULL");
        return;
    }

    // --- ВЫДЕЛЕНИЕ ПАМЯТИ ---
    Serial.println("Allocating memory...");
    
    char* msg = (char*)malloc(2048);
    char* call_id = (char*)malloc(CALL_ID_LEN);
    char* from_header = (char*)malloc(256);
    char* from_tag_buf = (char*)malloc(TAG_LEN);
    char* to_header = (char*)malloc(256);
    char* cseq_str = (char*)malloc(32);
    char* first_via = (char*)malloc(256);
    char* contact_uri = (char*)malloc(128);

    // Проверка всех выделений
    if (!msg || !call_id || !from_header || !from_tag_buf || !to_header || !cseq_str || !first_via || !contact_uri) {
        Serial.println("ERROR: Failed to allocate buffers");
        if (msg) free(msg);
        if (call_id) free(call_id);
        if (from_header) free(from_header);
        if (from_tag_buf) free(from_tag_buf);
        if (to_header) free(to_header);
        if (cseq_str) free(cseq_str);
        if (first_via) free(first_via);
        if (contact_uri) free(contact_uri);
        return;
    }

    // Инициализация буферов
    memset(msg, 0, 2048);
    memset(call_id, 0, CALL_ID_LEN);
    memset(from_header, 0, 256);
    memset(from_tag_buf, 0, TAG_LEN);
    memset(to_header, 0, 256);
    memset(cseq_str, 0, 32);
    memset(first_via, 0, 256);
    memset(contact_uri, 0, 128);

    Serial.println("All buffers allocated successfully");

    // --- ИЗВЛЕЧЕНИЕ ЗАГОЛОВКОВ ---
    Serial.println("Extracting headers...");
    
    bool extraction_success = true;
    
    if (!extractSIPHeader(request, strlen(request), "Call-ID:", call_id, CALL_ID_LEN)) {
        Serial.println("ERROR: Failed to extract Call-ID");
        extraction_success = false;
    }
    if (extraction_success && !extractSIPHeader(request, strlen(request), "From:", from_header, 256)) {
        Serial.println("ERROR: Failed to extract From");
        extraction_success = false;
    }
    if (extraction_success && !extractSIPHeader(request, strlen(request), "To:", to_header, 256)) {
        Serial.println("ERROR: Failed to extract To");
        extraction_success = false;
    }
    if (extraction_success && !extractSIPHeader(request, strlen(request), "CSeq:", cseq_str, 32)) {
        Serial.println("ERROR: Failed to extract CSeq");
        extraction_success = false;
    }
    if (extraction_success && !extractFirstViaHeader(request, strlen(request), first_via, 256)) {
        Serial.println("ERROR: Failed to extract Via");
        extraction_success = false;
    }

    // Извлечение From tag (не критично)
    if (extraction_success) {
        if (!extractSIPHeader(request, strlen(request), "From:", from_tag_buf, TAG_LEN, "tag=")) {
            Serial.println("WARNING: Failed to extract From tag, using default");
            strcpy(from_tag_buf, "default_tag");
        }
    }

    if (!extraction_success) {
        Serial.println("ERROR: Header extraction failed, cleaning up");
        free(msg);
        free(call_id);
        free(from_header);
        free(from_tag_buf);
        free(to_header);
        free(cseq_str);
        free(first_via);
        free(contact_uri);
        return;
    }

    Serial.printf("Call-ID: %s\n", call_id);
    Serial.printf("From: %s\n", from_header);
    Serial.printf("To: %s\n", to_header);
    Serial.printf("CSeq: %s\n", cseq_str);
    Serial.printf("From tag: %s\n", from_tag_buf);
    Serial.printf("Via: %s\n", first_via);

    // --- ФОРМИРОВАНИЕ ОСНОВНЫХ ЗАГОЛОВКОВ ---
    Serial.println("Forming headers...");
    
    snprintf(contact_uri, 128, "<sip:%s@%s:%d>", 
             configManager->getSIPUsername(), local_ip, SIP_PORT);
    Serial.printf("Contact URI: %s\n", contact_uri);

    char final_to_tag[TAG_LEN] = {0};
    if (to_tag && strlen(to_tag) > 0) {
        strncpy(final_to_tag, to_tag, sizeof(final_to_tag) - 1);
    } else {
        snprintf(final_to_tag, sizeof(final_to_tag), "%lu", esp_random());
    }
    Serial.printf("Final To-tag: %s\n", final_to_tag);

    // Парсинг CSeq
    char* method_ptr = strchr(cseq_str, ' ');
    long cseq_num = 1;
    if (method_ptr) {
        *method_ptr = '\0';
        method_ptr++;
        cseq_num = atol(cseq_str);
    }
    Serial.printf("CSeq num: %ld, method: %s\n", cseq_num, method_ptr ? method_ptr : "UNKNOWN");

    // --- ФОРМИРОВАНИЕ ОТВЕТА ---
    Serial.println("Forming response...");
    
    int len = 0;
    bool response_formed = false;
    
    if (with_sdp) {
        Serial.println("Generating SDP...");
        char sdp_body[512];
        generateSDPBody(sdp_body, sizeof(sdp_body), local_ip, local_rtp_port);
        Serial.printf("SDP generated, length: %d\n", strlen(sdp_body));
        
        len = snprintf(msg, 2048,
            "SIP/2.0 %d %s\r\n"
            "Via: %s;received=%s;rport=%d\r\n"
            "From: %s\r\n"
            "To: %s;tag=%s\r\n"
            "Call-ID: %s\r\n"
            "CSeq: %ld %s\r\n"
            "Contact: %s\r\n"
            "User-Agent: ALINA-SIP/1.0\r\n"
            "Content-Type: application/sdp\r\n"
            "Content-Length: %d\r\n"
            "\r\n"
            "%s",
            code, reason,
            first_via, local_ip, SIP_PORT,
            from_header,
            to_header, final_to_tag,
            call_id,
            cseq_num, method_ptr ? method_ptr : "INVITE",
            contact_uri,
            strlen(sdp_body),
            sdp_body);
            
        response_formed = (len > 0 && len < 2048);
    } else {
        len = snprintf(msg, 2048,
            "SIP/2.0 %d %s\r\n"
            "Via: %s;received=%s;rport=%d\r\n"
            "From: %s\r\n"
            "To: %s;tag=%s\r\n"
            "Call-ID: %s\r\n"
            "CSeq: %ld %s\r\n"
            "Contact: %s\r\n"
            "User-Agent: ALINA-SIP/1.0\r\n"
            "Content-Length: 0\r\n"
            "\r\n",
            code, reason,
            first_via, local_ip, SIP_PORT,
            from_header,
            to_header, final_to_tag,
            call_id,
            cseq_num, method_ptr ? method_ptr : "INVITE",
            contact_uri);
            
        response_formed = (len > 0 && len < 2048);
    }

    Serial.printf("snprintf returned: %d\n", len);

    if (!response_formed) {
        Serial.println("ERROR: Failed to form response");
    } else {
        Serial.printf("Response formed, length: %d\n", strlen(msg));
        Serial.printf("Stack free before send: %d\n", esp_get_free_heap_size());

        // --- ОТПРАВКА ---
        Serial.println("Sending message...");
        sendSIPMessage(dst_ip, dst_port, msg);
        Serial.println("Message sent successfully");
    }

    // --- ОСВОБОЖДЕНИЕ ПАМЯТИ ---
    Serial.println("Cleaning up memory...");
    free(msg);
    free(call_id);
    free(from_header);
    free(from_tag_buf);
    free(to_header);
    free(cseq_str);
    free(first_via);
    free(contact_uri);
    
    Serial.printf("Stack free after cleanup: %d\n", esp_get_free_heap_size());
    Serial.println("=== sendResponse EXIT ===");
}

void EnhancedSIPClient::generateSDPBody(char* buffer, size_t buffer_size, const char* local_ip, uint16_t local_rtp_port) {
    Serial.printf("generateSDPBody: buffer_size=%d, local_ip=%s, local_rtp_port=%d\n", 
                  buffer_size, local_ip, local_rtp_port);
    
    if (!buffer || buffer_size < 100) {
        Serial.println("ERROR: Invalid buffer in generateSDPBody");
        return;
    }

    uint32_t session_id = esp_random();
    uint32_t version = esp_random();
    
    int len = snprintf(buffer, buffer_size,
        "v=0\r\n"
        "o=- %lu %lu IN IP4 %s\r\n"
        "s=ALINA SIP Client\r\n"
        "c=IN IP4 %s\r\n"
        "t=0 0\r\n"
        "m=audio %d RTP/AVP 8 101\r\n"
        "a=rtpmap:8 PCMA/8000\r\n"
        "a=rtpmap:101 telephone-event/8000\r\n"
        "a=fmtp:101 0-16\r\n"
        "a=sendrecv\r\n",
        session_id, version, local_ip,
        local_ip,
        local_rtp_port);
    
    Serial.printf("SDP generated, length: %d\n", len);
    
    if (len <= 0 || len >= (int)buffer_size) {
        Serial.println("ERROR: SDP buffer overflow");
        buffer[0] = '\0';
    }
}

bool EnhancedSIPClient::extractFirstViaHeader(const char* data, size_t len, char* output, size_t out_size) {
    Serial.printf("extractFirstViaHeader: data=%p, len=%d, out_size=%d\n", data, len, out_size);
    
    if (!data || len == 0) {
        Serial.println("ERROR: Invalid input data");
        return false;
    }

    const char* via_start = strstr(data, "Via:");
    if (!via_start) {
        Serial.println("ERROR: Via header not found");
        return false;
    }
    
    via_start += 4;
    while (*via_start == ' ' || *via_start == '\t') via_start++;
    
    const char* via_end = via_start;
    while (*via_end && via_end - data < (int)len) {
        if (*via_end == ',') break;
        if (*via_end == '\r' && *(via_end + 1) == '\n') break;
        via_end++;
    }
    
    size_t via_len = via_end - via_start;
    Serial.printf("Via length: %d\n", via_len);
    
    if (via_len == 0 || via_len >= out_size) {
        Serial.printf("ERROR: Invalid Via length: %d (max: %d)\n", via_len, out_size);
        return false;
    }

    strncpy(output, via_start, via_len);
    output[via_len] = '\0';
    
    // Очистка от концевых пробелов
    char* last_char = output + strlen(output) - 1;
    while (last_char >= output && (*last_char == ' ' || *last_char == '\r' || *last_char == '\n')) {
        *last_char = '\0';
        last_char--;
    }
    
    Serial.printf("Extracted Via: %s\n", output);
    return true;
}

void EnhancedSIPClient::sendACK(call_t* call) {
    if (!call || !networkManager || !networkManager->isConnected()) return;

    char msg[512];
    const char* local_ip = networkManager->getLocalIP();
    // ВАЖНО: Проверяем, что IP не 0.0.0.0 перед отправкой
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: sendACK: Локальный IP 0.0.0.0, невозможно отправить ACK");
        return; // Пропускаем отправку
    }
    uint32_t branch = esp_random();
    // CSeq для ACK должен быть равен CSeq INVITE
    uint32_t ack_cseq = call->cseq_invite;

    int len = snprintf(msg, sizeof(msg),
                       "ACK %s SIP/2.0\r\n"
                       "Via: SIP/2.0/UDP %s:%d;branch=z9hG4bK%lu;rport\r\n"
                       "From: <sip:%s@%s>;tag=%s\r\n"
                       "To: <sip:%s@%s>;tag=%s\r\n" // Используем To-tag из 200 OK
                       "Call-ID: %s\r\n"
                       "CSeq: %lu ACK\r\n"
                       "User-Agent: ALINA/1.0\r\n"
                       "Content-Length: 0\r\n\r\n",
                       call->contact_uri, // Используем Contact URI для отправки ACK
                       local_ip, SIP_PORT, branch, // <-- Вот тут будет правильный IP
                       sip_user, sip_server, call->from_tag,
                       sip_user, sip_server, call->to_tag,
                       call->call_id,
                       ack_cseq);

    if (len > 0 && len < (int)sizeof(msg)) {
        Serial.printf("SIP: Отправляем ACK:\n%s", msg);
        // Отправляем ACK на IP и порт, указанные в Contact URI из 200 OK
        // Если есть Record-Route, нужно отправить через прокси
        const char* target_ip = call->remote_ip; // По умолчанию
        uint16_t target_port = call->remote_sip_port;
        if (strlen(call->contact_uri) > 0) {
             // Если есть Contact URI, парсим его для определения конечного адресата ACK
             char contact_ip[16];
             uint16_t contact_port;
             parseContactURI(call->contact_uri, contact_ip, &contact_port);
             if (strlen(contact_ip) > 0) {
                 target_ip = contact_ip;
                 target_port = contact_port;
             }
             Serial.printf("SIP: Отправляем ACK на %s:%d (из Contact URI)\n", target_ip, target_port);
        }
        sendSIPMessage(target_ip, target_port, msg);
    } else {
        Serial.println("SIP: Ошибка: ACK сообщение слишком длинное");
    }
}

// EnhancedSIPClient.cpp
bool EnhancedSIPClient::extractSIPHeader(const char* data, size_t len, const char* header, char* output, size_t out_size, const char* sub) {
    output[0] = '\0';

    const char* ptr = strcasestr(data, header);
    if (!ptr) return false;
    ptr += strlen(header);

    // Пропускаем пробелы
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    // Для Via заголовков - особая обработка
    if (strcasecmp(header, "Via:") == 0) {
        const char* end = ptr;
        while (*end && *end != '\n' && end - data < (int)len) {
            if (*end == '\r' && *(end + 1) == '\n') break;
            if (*end == ',') break; // Останавливаемся на запятой для Via
            end++;
        }
        
        size_t value_len = end - ptr;
        size_t copy_len = (value_len < out_size - 1) ? value_len : out_size - 1;
        strncpy(output, ptr, copy_len);
        output[copy_len] = '\0';
        
        // Очистка от концевых пробелов
        char* last = output + strlen(output) - 1;
        while (last >= output && (*last == ' ' || *last == '\r' || *last == '\n')) {
            *last = '\0';
            last--;
        }
        return true;
    }

    // Оригинальная логика для других заголовков
    if (*ptr == '<') ptr++;

    if (sub) {
        ptr = strstr(ptr, sub);
        if (!ptr) return false;
        ptr += strlen(sub);
        while (*ptr == '=' || *ptr == '"' || *ptr == ' ') ptr++;
    }

    const char* end = ptr;
    while (*end && *end != '\n' && end - data < (int)len) {
        if (*end == '\r' && *(end + 1) == '\n') break;
        if (*end == ';' || *end == '>') break;
        end++;
    }

    size_t value_len = end - ptr;
    size_t copy_len = (value_len < out_size - 1) ? value_len : out_size - 1;
    strncpy(output, ptr, copy_len);
    output[copy_len] = '\0';

    // Очистка
    char* last = output + strlen(output) - 1;
    while (last >= output && (*last == ' ' || *last == '>' || *last == '\r' || *last == '\n' || *last == ';')) {
        *last = '\0';
        last--;
    }

    return true;
}


void EnhancedSIPClient::parseAuthHeader(const char* header, auth_info_t* auth) {
    if (!header || !auth) return;

    // Инициализируем структуру
    memset(auth, 0, sizeof(auth_info_t));

    // Создаем копию строки для strtok_r
    char header_copy[512];
    strncpy(header_copy, header, sizeof(header_copy) - 1);
    header_copy[sizeof(header_copy) - 1] = '\0';

    char* save_ptr;
    char* param = strtok_r(header_copy, ",", &save_ptr);

    while (param) {
        // Удаляем лишние пробелы в начале и конце параметра
        while (*param == ' ') param++;
        char* end = param + strlen(param) - 1;
        while (end > param && (*end == ' ' || *end == '\r' || *end == '\n')) end--;
        *(end + 1) = '\0';

        // Парсим имя параметра и значение
        char* sep = strchr(param, '=');
        if (sep) {
            *sep = '\0';
            char* name = param;
            char* value = sep + 1;

            // Удаляем кавычки из значения
            if (*value == '"') {
                value++;
                char* end_quote = strchr(value, '"');
                if (end_quote) *end_quote = '\0';
            }

            if (strcasecmp(name, "realm") == 0) {
                strncpy(auth->realm, value, sizeof(auth->realm) - 1);
                auth->realm[sizeof(auth->realm) - 1] = '\0';
            } else if (strcasecmp(name, "nonce") == 0) {
                strncpy(auth->nonce, value, sizeof(auth->nonce) - 1);
                auth->nonce[sizeof(auth->nonce) - 1] = '\0';
            } else if (strcasecmp(name, "qop") == 0) {
                strncpy(auth->qop, value, sizeof(auth->qop) - 1);
                auth->qop[sizeof(auth->qop) - 1] = '\0';
            }
            // УДАЛЕНО: else if (strcasecmp(name, "algorithm") == 0) { ... }
            else if (strcasecmp(name, "opaque") == 0) {
                strncpy(auth->opaque, value, sizeof(auth->opaque) - 1);
                auth->opaque[sizeof(auth->opaque) - 1] = '\0';
            }
        }
        param = strtok_r(nullptr, ",", &save_ptr);
    }
}

/**
 * ПОЛНОЦЕННАЯ РЕАЛИЗАЦИЯ Digest аутентификации MD5
 * Соответствует RFC 2617
 */
// char* EnhancedSIPClient::calculateResponse(const char* username, const char* realm, const char* password,
//                                           const char* method, const char* uri, const char* nonce,
//                                           const char* qop, const char* nc, const char* cnonce) {
    
//     // // ВАЛИДАЦИЯ ВХОДНЫХ ПАРАМЕТРОВ
//     // if (!username || !realm || !password || !method || !uri || !nonce) {
//     //     Serial.println("SIP: Ошибка calculateResponse - невалидные параметры");
//     //     return nullptr;
//     // }

//     // // ПРОВЕРКА ДЛИНЫ СТРОК
//     // if (strlen(username) == 0 || strlen(realm) == 0 || strlen(method) == 0 || strlen(uri) == 0 || strlen(nonce) == 0) {
//     //     Serial.println("SIP: Ошибка calculateResponse - пустые параметры");
//     //     return nullptr;
//     // }

//     // char ha1_input[512];
//     // char ha1_hex[33] = {0}, ha2_hex[33] = {0}, response_hex[33] = {0};
//     // unsigned char ha1[16], ha2[16], response[16];
    
//     // // Шаг 1: HA1 = MD5(username:realm:password)
//     // int ha1_len = snprintf(ha1_input, sizeof(ha1_input), "%s:%s:%s", username, realm, password);
//     // if (ha1_len < 0 || ha1_len >= (int)sizeof(ha1_input)) {
//     //     Serial.println("SIP: Ошибка calculateResponse - HA1 input слишком длинный");
//     //     return nullptr;
//     // }

//     // mbedtls_md5_context md5_ctx;
//     // mbedtls_md5_init(&md5_ctx);
//     // mbedtls_md5_starts(&md5_ctx);
//     // mbedtls_md5_update(&md5_ctx, (const unsigned char*)ha1_input, strlen(ha1_input));
//     // mbedtls_md5_finish(&md5_ctx, ha1);
//     // mbedtls_md5_free(&md5_ctx);
    
//     // // Конвертация в hex
//     // for (int i = 0; i < 16; i++) {
//     //     sprintf(&ha1_hex[i * 2], "%02x", ha1[i]);
//     // }
//     // ha1_hex[32] = '\0';
    
//     // // Шаг 2: HA2 = MD5(method:uri)
//     // char ha2_input[512];
//     // int ha2_len = snprintf(ha2_input, sizeof(ha2_input), "%s:%s", method, uri);
//     // if (ha2_len < 0 || ha2_len >= (int)sizeof(ha2_input)) {
//     //     Serial.println("SIP: Ошибка calculateResponse - HA2 input слишком длинный");
//     //     return nullptr;
//     // }

//     // mbedtls_md5_init(&md5_ctx);
//     // mbedtls_md5_starts(&md5_ctx);
//     // mbedtls_md5_update(&md5_ctx, (const unsigned char*)ha2_input, strlen(ha2_input));
//     // mbedtls_md5_finish(&md5_ctx, ha2);
//     // mbedtls_md5_free(&md5_ctx);
    
//     // for (int i = 0; i < 16; i++) {
//     //     sprintf(&ha2_hex[i * 2], "%02x", ha2[i]);
//     // }
//     // ha2_hex[32] = '\0';
    
//     // // Шаг 3: Response = MD5(HA1:nonce:HA2) или с QOP
//     // char response_input[1024];
//     // bool qop_enabled = (qop && strlen(qop) > 0 && nc && cnonce);
    
//     // int response_input_len;
//     // if (qop_enabled) {
//     //     response_input_len = snprintf(response_input, sizeof(response_input), "%s:%s:%s:%s:%s:%s", 
//     //             ha1_hex, nonce, nc, cnonce, qop, ha2_hex);
//     // } else {
//     //     response_input_len = snprintf(response_input, sizeof(response_input), "%s:%s:%s", 
//     //             ha1_hex, nonce, ha2_hex);
//     // }
    
//     // if (response_input_len < 0 || response_input_len >= (int)sizeof(response_input)) {
//     //     Serial.println("SIP: Ошибка calculateResponse - response input слишком длинный");
//     //     return nullptr;
//     // }

//     // mbedtls_md5_init(&md5_ctx);
//     // mbedtls_md5_starts(&md5_ctx);
//     // mbedtls_md5_update(&md5_ctx, (const unsigned char*)response_input, strlen(response_input));
//     // mbedtls_md5_finish(&md5_ctx, response);
//     // mbedtls_md5_free(&md5_ctx);
    
//     // for (int i = 0; i < 16; i++) {
//     //     sprintf(&response_hex[i * 2], "%02x", response[i]);
//     // }
//     // response_hex[32] = '\0';
    
//     // // Выделение памяти для результата
//     // char* response_str = (char*)malloc(33);
//     // if (response_str) {
//     //     strcpy(response_str, response_hex);
//     //     Serial.printf("SIP: Digest response вычислен: %s\n", response_str);
//     // } else {
//     //     Serial.println("SIP: Ошибка выделения памяти для Digest response");
//     // }
    
//     // return response_str;
// }

int EnhancedSIPClient::findFreeCallSlot() {
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state == CALL_STATE_IDLE) {
            return i;
        }
    }
    return -1; // Нет свободных слотов
}

void EnhancedSIPClient::resetCall(call_t* call) {
    if (!call) {
        Serial.println("SIP DEBUG: resetCall called with nullptr!");
        return;
    }
    // ВАЖНО: Обнуляем всю структуру
    memset(call, 0, sizeof(call_t));
    // Устанавливаем начальное состояние
    call->state = CALL_STATE_IDLE;
    // Убедимся, что строковые буферы завершены нулем (хотя memset уже это сделал)
    call->remote_ip[0] = '\0';
    call->record_route[0] = '\0'; // Убедимся, что record_route пуст
    call->call_id[0] = '\0';
    call->from_uri[0] = '\0';
    call->from_tag[0] = '\0';
    call->to_uri[0] = '\0';
    call->to_tag[0] = '\0';
    call->contact_uri[0] = '\0';
    // И другие строковые поля, если есть
    Serial.printf("SIP DEBUG: resetCall completed for call ID %d.\n", call->id); // ID может быть неинициализирован, но это ок
}


void EnhancedSIPClient::sendSIPMessage(const char* ip, uint16_t port, const char* msg) {
    // ВАЖНО: Проверяем, что сеть подключена перед отправкой
    if (!networkManager || !networkManager->isConnected()) {
        Serial.printf("SIP: sendSIPMessage: Сеть не подключена, не отправляю на %s:%d\n", ip, port);
        return;
    }

    // Проверка на пустой или невалидный IP
    if (!ip || strlen(ip) == 0 || strcmp(ip, "0.0.0.0") == 0) {
        Serial.printf("SIP: Ошибка - невалидный IP для отправки: '%s'\n", ip ? ip : "NULL");
        return;
    }

    IPAddress addr;
    if (addr.fromString(ip)) {
        bool success = networkManager->udp.writeTo((uint8_t*)msg, strlen(msg), addr, port);
        if (!success) {
            Serial.printf("SIP: Ошибка отправки SIP сообщения на %s:%d\n", ip, port);
        } else {
            Serial.printf("SIP: Сообщение отправлено на %s:%d\n", ip, port);
        }
    } else {
        Serial.printf("SIP: Неверный IP-адрес: %s\n", ip);
    }
}
void EnhancedSIPClient::parseContactURI(const char* contact_uri, char* ip, uint16_t* port) {
    // Пример: <sip:user@192.168.1.100:5060>
    // Инициализация выходных значений
    ip[0] = '\0';
    *port = 5060;

    if (!contact_uri || strlen(contact_uri) == 0) {
        return;
    }
    
    // Ищем @
    const char* at = strchr(contact_uri, '@');
    if (!at) {
         // Если @ нет, ищем IP в формате sip:192.168.1.100:5060
         const char* sip_prefix = strstr(contact_uri, "sip:");
         if (sip_prefix) {
             sip_prefix += 4; // Пропускаем "sip:"
             const char* colon = strchr(sip_prefix, ':');
             if (colon) {
                 size_t ip_len = colon - sip_prefix;
                 if (ip_len < 16) {
                     strncpy(ip, sip_prefix, ip_len);
                     ip[ip_len] = '\0';
                     *port = atoi(colon + 1);
                     return;
                 }
             } else {
                 // Нет порта, используем 5060
                 size_t ip_len = strlen(sip_prefix);
                 if (ip_len < 16) {
                     strncpy(ip, sip_prefix, ip_len);
                     ip[ip_len] = '\0';
                     *port = 5060;
                     return;
                 }
             }
         }
        return; // Не удалось распознать
    }
    at++; // Пропускаем @

    // Ищем :
    const char* colon = strchr(at, ':');
    if (colon) {
        size_t ip_len = colon - at;
        if (ip_len < 16) { // Проверяем длину IP
            strncpy(ip, at, ip_len);
            ip[ip_len] = '\0';
            *port = atoi(colon + 1);
        }
    } else {
        // Нет порта, используем 5060
        size_t ip_len = strcspn(at, ">;"); // Ищем конец IP (до > или ;)
        if (ip_len < 16) {
            strncpy(ip, at, ip_len);
            ip[ip_len] = '\0';
            *port = 5060;
        }
    }
}

// --- ГЕТТЕРЫ ---

const char* EnhancedSIPClient::getLocalIP() {
    if (networkManager) {
        return networkManager->getLocalIP();
    }
    return "0.0.0.0";
}



// --- СБРОС АУТЕНТИФИКАЦИИ ---

void EnhancedSIPClient::resetAuth() {
    require_auth = false;
    memset(&auth_info, 0, sizeof(auth_info));
    Serial.println("SIP: Сброшена информация аутентификации SIP");
}


// --- Реализация новых публичных методов-геттеров ---

call_state_t EnhancedSIPClient::getCallState(int call_index) const {
    if (call_index < 0 || call_index >= max_calls) {
        return CALL_STATE_IDLE; // Возвращаем безопасное значение для неверного индекса
    }
    return calls[call_index].state;
}

const char* EnhancedSIPClient::getCallId(int call_index) const {
    if (call_index < 0 || call_index >= max_calls) {
        return nullptr; // Возвращаем nullptr для неверного индекса
    }
    return calls[call_index].call_id;
}

const char* EnhancedSIPClient::getRemoteIP(int call_index) const {
    if (call_index < 0 || call_index >= max_calls) {
        return nullptr; // Возвращаем nullptr для неверного индекса
    }
    return calls[call_index].remote_ip;
}

int EnhancedSIPClient::getFirstActiveCallId() const {
    for (int i = 0; i < max_calls; i++) {
        if (calls[i].state != CALL_STATE_IDLE) {
            return i;
        }
    }
    return -1;
}


/**
 * ВАЛИДАЦИЯ СЕТЕВОГО ПОДКЛЮЧЕНИЯ
 * Оптимизировано для WT32-ETH01
 */
bool EnhancedSIPClient::validateNetwork() const {
    if (!networkManager) {
        Serial.println("SIP: validateNetwork - networkManager is null");
        return false;
    }
    
    bool connected = networkManager->isConnected();
    const char* ip = networkManager->getLocalIP();
    
    if (!connected) {
        Serial.println("SIP: Сеть не подключена");
        return false;
    }
    
    if (strcmp(ip, "0.0.0.0") == 0) {
        Serial.println("SIP: IP адрес не получен (0.0.0.0)");
        return false;
    }
    
    Serial.printf("SIP: Сеть валидна, IP: %s\n", ip);
    return true;
}

/**
 * ВАЛИДАЦИЯ УЧЕТНЫХ ДАННЫХ SIP
 */
bool EnhancedSIPClient::validateSIPCredentials() const {
    if (strlen(sip_user) == 0) {
        Serial.println("SIP: Не задан SIP пользователь");
        return false;
    }
    
    if (strlen(sip_server) == 0) {
        Serial.println("SIP: Не задан SIP сервер");
        return false;
    }
    
    if (strlen(sip_password) == 0) {
        Serial.println("SIP: Предупреждение - пустой SIP пароль");
    }
    
    return true;
}


/**
 * Получить From URI вызова (для WebInterface)
 */
const char* EnhancedSIPClient::getFromUri(int call_index) const {
    if (call_index < 0 || call_index >= max_calls) {
        return nullptr;
    }
    return calls[call_index].from_uri;
}

// EnhancedSIPClient.cpp (внутри класса EnhancedSIPClient, после других методов)

void EnhancedSIPClient::handle200OK(const char* data, size_t len, const char* remote_ip, uint16_t remote_port) {
    Serial.println("SIP: handle200OK вызван для обработки 200 OK INVITE");

    // Найти вызов, соответствующий CSeq из 200 OK
    char cseq_str[16];
    if (extractSIPHeader(data, len, "CSeq:", cseq_str, sizeof(cseq_str))) {
        int cseq_num = atoi(cseq_str);
        call_t* call = nullptr;
        for (int i = 0; i < max_calls; i++) {
            // Проверяем, что вызов не IDLE и его CSeq INVITE совпадает
            if (calls[i].state != CALL_STATE_IDLE && calls[i].cseq_invite == cseq_num) {
                call = &calls[i];
                break;
            }
        }
        if (call) {
            Serial.printf("SIP: Найден вызов %d (состояние: %d) для 200 OK INVITE\n", call->id, call->state);

            // Извлечение To-tag из 200 OK и сохранение в структуре вызова
            extractSIPHeader(data, len, "To:", call->to_tag, sizeof(call->to_tag), "tag=");
            Serial.printf("SIP: Установлен To-tag для вызова %d: %s\n", call->id, call->to_tag);

            // Извлечение Record-Route (если есть) и сохранение в структуре вызова
            // char record_route[256]; // <-- УДАЛЕНО: используем поле структуры
            if (extractSIPHeader(data, len, "Record-Route:", call->record_route, sizeof(call->record_route))) { // <-- ИСПРАВЛЕНО: используем поле структуры
                Serial.printf("SIP: Сохранен Record-Route: %s\n", call->record_route); // <-- ИСПРАВЛЕНО: используем поле структуры
            }

            // Извлечение Contact URI из 200 OK для отправки ACK
            // char contact_uri[256]; // <-- УДАЛЕНО: используем поле структуры
            if (extractSIPHeader(data, len, "Contact:", call->contact_uri, sizeof(call->contact_uri))) { // <-- ИСПРАВЛЕНО: используем поле структуры
                // Парсим IP и порт из Contact URI и обновляем вызов
                char temp_ip[IP_LEN]; // Временный буфер для IP
                uint16_t temp_port;
                parseContactURI(call->contact_uri, temp_ip, &temp_port); // <-- Используем поле структуры
                if (strlen(temp_ip) > 0) {
                    strncpy(call->remote_ip, temp_ip, sizeof(call->remote_ip) - 1); // Обновляем IP вызова
                    call->remote_ip[sizeof(call->remote_ip) - 1] = '\0';
                    call->remote_sip_port = temp_port; // Обновляем порт вызова
                    Serial.printf("SIP: Обновлен Contact URI для вызова %d: IP: %s, Port: %d\n", // <-- \n добавлен
                                  call->id, call->remote_ip, call->remote_sip_port);
                }
            }

            // Отправить ACK
            sendACK(call); // <-- Вызов sendACK

            // Перейти в состояние активного разговора
            call->state = CALL_STATE_ACTIVE; // <-- ИСПРАВЛЕНО: используем новое состояние, если определено
            call->last_activity = millis();
            Serial.printf("SIP: Вызов %d переведён в состояние ACTIVE (ACK отправлен)\n", call->id); // <-- \n добавлен
        } else {
            Serial.println("SIP: Ошибка: Не найден вызов для 200 OK INVITE\n"); // <-- \n добавлен
        }
    } else {
        Serial.println("SIP: handle200OK: Не найден заголовок CSeq в 200 OK INVITE\n"); // <-- \n добавлен
    }
}

// --- ОТПРАВКА 100 TRYING ---
void EnhancedSIPClient::sendTrying(const char* request, const char* dst_ip, uint16_t dst_port) {
    if (!networkManager || !networkManager->isConnected()) {
        Serial.println("SIP: sendTrying: Сеть не подключена, не отправляю 100 Trying");
        return;
    }

    const char* local_ip = networkManager->getLocalIP();
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: sendTrying: Локальный IP 0.0.0.0, не отправляю 100 Trying");
        return;
    }

    // - ВЫДЕЛЕНИЕ БУФЕРОВ В КУЧЕ -
    char* msg = (char*)malloc(512 * sizeof(char));
    char* call_id = (char*)malloc(CALL_ID_LEN * sizeof(char));
    // Буфер для извлечения полного From заголовка (включая тег)
    char* from_header_full = (char*)malloc(256 * sizeof(char));
    // Буфер для URI From без тега
    char* from_uri_only = (char*)malloc(256 * sizeof(char));
    // Буфер для тега From
    char* from_tag = (char*)malloc(TAG_LEN * sizeof(char));
    // Буфер для извлечения *всех* Via заголовков
    char* via_headers = (char*)malloc(512 * sizeof(char)); // Должно хватить на несколько Via
    // Буфер для извлечения *нового* branch для ответа
    char* new_via_branch = (char*)malloc(64 * sizeof(char));
    // Буфер для извлечения полного To заголовка (включая тег, если есть)
    char* to_header_full = (char*)malloc(256 * sizeof(char));
    // Буфер для URI To без тега
    char* to_uri_only = (char*)malloc(256 * sizeof(char));
    // Буфер для извлечения *всех* Record-Route заголовков
    char* record_route_headers = (char*)malloc(512 * sizeof(char)); // Должно хватить на несколько Record-Route

    if (!msg || !call_id || !from_header_full || !from_uri_only || !from_tag || !via_headers || !new_via_branch || !to_header_full || !to_uri_only || !record_route_headers) {
        Serial.println("SIP: sendTrying: Ошибка выделения памяти для буферов");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
        return;
    }

    // Инициализация
    memset(msg, 0, 512);
    memset(call_id, 0, CALL_ID_LEN);
    memset(from_header_full, 0, 256);
    memset(from_uri_only, 0, 256);
    memset(from_tag, 0, TAG_LEN);
    memset(via_headers, 0, 512);
    memset(new_via_branch, 0, 64);
    memset(to_header_full, 0, 256);
    memset(to_uri_only, 0, 256);
    memset(record_route_headers, 0, 512);

    // - ИЗВЛЕЧЕНИЕ ЗАГОЛОВКОВ -
    if (!extractSIPHeader(request, strlen(request), "Call-ID:", call_id, CALL_ID_LEN) ||
        !extractSIPHeader(request, strlen(request), "From:", from_header_full, 256) || // Извлекаем полный From
        !extractSIPHeader(request, strlen(request), "From:", from_tag, TAG_LEN, "tag=") || // Извлекаем только тег
        !extractSIPHeader(request, strlen(request), "Via:", via_headers, 512) || // Извлекаем все Via
        !extractSIPHeader(request, strlen(request), "To:", to_header_full, 256)) { // Извлекаем полный To
        Serial.println("SIP: sendTrying: Ошибка извлечения заголовков для 100 Trying");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
        return;
    }

    // Извлечение *всех* Record-Route заголовков
    if (extractSIPHeader(request, strlen(request), "Record-Route:", record_route_headers, 512)) { // Извлекаем все Record-Route, если есть
        Serial.printf("SIP DEBUG: sendTrying - extracted record_route_headers (all): %s", record_route_headers);
    } else {
        Serial.println("SIP DEBUG: sendTrying - no Record-Route headers found in request");
        record_route_headers[0] = '\0'; // Убедимся, что буфер пуст, если не нашли
    }

    // --- ОЧИСТКА To URI от тега (для 100 Trying To-tag не ставится) ---
    strncpy(to_uri_only, to_header_full, 256 - 1);
    to_uri_only[256 - 1] = '\0'; // Обеспечиваем завершение строки
    char* tag_pos_to = strstr(to_uri_only, ";tag=");
    if (tag_pos_to) {
        *tag_pos_to = '\0'; // Обрезаем строку на месте ";tag="
    }

    // --- ОЧИСТКА From URI от тега ---
    strncpy(from_uri_only, from_header_full, 256 - 1);
    from_uri_only[256 - 1] = '\0'; // Обеспечиваем завершение строки
    char* tag_pos_from = strstr(from_uri_only, ";tag=");
    if (tag_pos_from) {
        *tag_pos_from = '\0'; // Обрезаем строку на месте ";tag="
    }

    char cseq_str[32] = {0}; // Этот буфер мал, можно оставить на стеке
    if (!extractSIPHeader(request, strlen(request), "CSeq:", cseq_str, sizeof(cseq_str))) {
        Serial.println("SIP: sendTrying: Ошибка извлечения CSeq для 100 Trying");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
        return;
    }

    char* end_ptr;
    long cseq_num = strtol(cseq_str, &end_ptr, 10);
    if (cseq_num <= 0 || cseq_num > 0x7FFFFFFF) {
        Serial.println("SIP: sendTrying: Ошибка извлечения CSeq числа для 100 Trying");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
        return;
    }
    const char* method = end_ptr;
    while (*method == ' ' || *method == '\t') method++;

    // Генерация *нового* branch для *нового* Via заголовка в ответе
    snprintf(new_via_branch, 64, "z9hG4bK%lu", esp_random());

    // - ФОРМИРОВАНИЕ СООБЩЕНИЯ -
    int len = snprintf(msg, 512, // Используем размер выделенного буфера
        "SIP/2.0 100 Trying\r"
        "Via: SIP/2.0/UDP %s:%d;branch=%s\r" // <-- НОВЫЙ Via заголовок в начале
        "%s" // <-- Копия всех остальных Via заголовков
        "From: %s;tag=%s\r" // <-- ИСПОЛЬЗУЕМ from_uri_only (без тега) и from_tag (тег)
        "To: %s\r"   // <-- ИСПОЛЬЗУЕМ to_uri_only (без тега)
        "Call-ID: %s\r"
        "CSeq: %ld %s\r"
        "%s" // <-- Копия всех Record-Route заголовков (если были)
        "Content-Length: 0\r\r",
        local_ip, SIP_PORT, new_via_branch, // <-- НОВЫЙ Via: IP, порт, *новый* branch
        (strlen(via_headers) > 0) ? via_headers : "", // <-- Копия остальных Via, если были
        from_uri_only, from_tag, // <-- ИСПРАВЛЕНО: URI без тега и тег отдельно
        to_uri_only, // <-- Без тега
        call_id,
        cseq_num, method,
        (strlen(record_route_headers) > 0) ? record_route_headers : ""); // <-- Копия Record-Route, если были

    if (len < 0 || len >= 512) { // Проверка переполнения snprintf
        Serial.println("SIP: sendTrying: Переполнение буфера при формировании 100 Trying");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
        return;
    }

    Serial.printf("SIP: Отправляем 100 Trying:%s", msg);
    sendSIPMessage(dst_ip, dst_port, msg);

    // - ОСВОБОЖДЕНИЕ ВЫДЕЛЕННОЙ ПАМЯТИ -
    free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers);
}

// --- ОТПРАВКА 180 RINGING ---
void EnhancedSIPClient::sendRinging(const char* request, const char* dst_ip, uint16_t dst_port, const char* to_tag) {
    if (!networkManager || !networkManager->isConnected()) {
        Serial.println("SIP: sendRinging: Сеть не подключена, не отправляю 180 Ringing");
        return;
    }

    const char* local_ip = networkManager->getLocalIP();
    if (strcmp(local_ip, "0.0.0.0") == 0) {
        Serial.println("SIP: sendRinging: Локальный IP 0.0.0.0, не отправляю 180 Ringing");
        return;
    }

    // - ВЫДЕЛЕНИЕ БУФЕРОВ В КУЧЕ -
    char* msg = (char*)malloc(1024 * sizeof(char));
    char* call_id = (char*)malloc(CALL_ID_LEN * sizeof(char));
    char* from_header_full = (char*)malloc(256 * sizeof(char));
    char* from_uri_only = (char*)malloc(256 * sizeof(char));
    char* from_tag = (char*)malloc(TAG_LEN * sizeof(char));
    char* via_headers = (char*)malloc(512 * sizeof(char));
    char* new_via_branch = (char*)malloc(64 * sizeof(char));
    char* to_header_full = (char*)malloc(256 * sizeof(char));
    char* to_uri_only = (char*)malloc(256 * sizeof(char));
    char* record_route_headers = (char*)malloc(512 * sizeof(char));
    char* contact_uri = (char*)malloc(URI_LEN * sizeof(char));

    if (!msg || !call_id || !from_header_full || !from_uri_only || !from_tag || !via_headers || !new_via_branch || !to_header_full || !to_uri_only || !record_route_headers || !contact_uri) {
        Serial.println("SIP: sendRinging: Ошибка выделения памяти для буферов");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
        return;
    }

    // Инициализация
    memset(msg, 0, 1024);
    memset(call_id, 0, CALL_ID_LEN);
    memset(from_header_full, 0, 256);
    memset(from_uri_only, 0, 256);
    memset(from_tag, 0, TAG_LEN);
    memset(via_headers, 0, 512);
    memset(new_via_branch, 0, 64);
    memset(to_header_full, 0, 256);
    memset(to_uri_only, 0, 256);
    memset(record_route_headers, 0, 512);
    memset(contact_uri, 0, URI_LEN);

    // - ИЗВЛЕЧЕНИЕ ЗАГОЛОВКОВ -
    if (!extractSIPHeader(request, strlen(request), "Call-ID:", call_id, CALL_ID_LEN) ||
        !extractSIPHeader(request, strlen(request), "From:", from_header_full, 256) ||
        !extractSIPHeader(request, strlen(request), "From:", from_tag, TAG_LEN, "tag=") ||
        !extractSIPHeader(request, strlen(request), "Via:", via_headers, 512) ||
        !extractSIPHeader(request, strlen(request), "To:", to_header_full, 256)) {
        Serial.println("SIP: sendRinging: Ошибка извлечения заголовков для 180 Ringing");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
        return;
    }

    // Извлечение Record-Route заголовков
    if (extractSIPHeader(request, strlen(request), "Record-Route:", record_route_headers, 512)) {
        Serial.printf("SIP DEBUG: sendRinging - extracted record_route_headers (all): %s\n", record_route_headers);
    } else {
        Serial.println("SIP DEBUG: sendRinging - no Record-Route headers found in request");
        record_route_headers[0] = '\0';
    }

    // --- ОЧИСТКА To URI от тега ---
    strncpy(to_uri_only, to_header_full, 256 - 1);
    to_uri_only[256 - 1] = '\0';
    char* tag_pos_to = strstr(to_uri_only, ";tag=");
    if (tag_pos_to) {
        *tag_pos_to = '\0';
    }

    // --- ОЧИСТКА From URI от тега ---
    strncpy(from_uri_only, from_header_full, 256 - 1);
    from_uri_only[256 - 1] = '\0';
    char* tag_pos_from = strstr(from_uri_only, ";tag=");
    if (tag_pos_from) {
        *tag_pos_from = '\0';
    }

    char cseq_str[32] = {0};
    if (!extractSIPHeader(request, strlen(request), "CSeq:", cseq_str, sizeof(cseq_str))) {
        Serial.println("SIP: sendRinging: Ошибка извлечения CSeq для 180 Ringing");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
        return;
    }

    char* end_ptr;
    long cseq_num = strtol(cseq_str, &end_ptr, 10);
    if (cseq_num <= 0 || cseq_num > 0x7FFFFFFF) {
        Serial.println("SIP: sendRinging: Ошибка извлечения CSeq числа для 180 Ringing");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
        return;
    }
    const char* method = end_ptr;
    while (*method == ' ' || *method == '\t') method++;

    // --- ОПРЕДЕЛЕНИЕ To-tag ---
    // КРИТИЧЕСКИ ВАЖНО: используем переданный to_tag или генерируем новый
    char to_tag_buf[TAG_LEN] = {0};
    if (to_tag && strlen(to_tag) > 0) {
        strncpy(to_tag_buf, to_tag, sizeof(to_tag_buf) - 1);
        to_tag_buf[sizeof(to_tag_buf) - 1] = '\0';
        Serial.printf("SIP: sendRinging - Используется переданный To-tag: %s\n", to_tag_buf);
    } else {
        snprintf(to_tag_buf, sizeof(to_tag_buf), "%lu", esp_random());
        Serial.printf("SIP: sendRinging - Сгенерирован новый To-tag: %s\n", to_tag_buf);
    }

    // --- ФОРМИРОВАНИЕ Contact URI ---
    snprintf(contact_uri, URI_LEN, "<sip:%s@%s:%d>", 
             configManager->getSIPUsername(), local_ip, SIP_PORT);

    // Генерация нового branch для Via заголовка в ответе
    snprintf(new_via_branch, 64, "z9hG4bK%lu", esp_random());

    // - ФОРМИРОВАНИЕ СООБЩЕНИЯ -
    int len = snprintf(msg, 1024,
        "SIP/2.0 180 Ringing\r\n"
        "Via: %s;received=%s;rport=%d\r\n"  // Используем полученный Via + received/rport
        "From: %s;tag=%s\r\n"
        "To: %s;tag=%s\r\n"
        "Call-ID: %s\r\n"
        "CSeq: %ld %s\r\n"
        "%s"  // Record-Route заголовки (если есть)
        "Contact: %s\r\n"
        "User-Agent: ALINA-SIP/1.0\r\n"
        "Content-Length: 0\r\n"
        "\r\n",
        via_headers, local_ip, SIP_PORT,    // Via с received и rport
        from_uri_only, from_tag,            // From URI и tag
        to_uri_only, to_tag_buf,            // To URI и tag (используем определенный выше)
        call_id,
        cseq_num, method,
        (strlen(record_route_headers) > 0) ? record_route_headers : "",
        contact_uri);

    if (len < 0 || len >= 1024) {
        Serial.println("SIP: sendRinging: Переполнение буфера при формировании 180 Ringing");
        free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
        return;
    }

    Serial.printf("SIP: Отправляем 180 Ringing:\n%s\n", msg);
    sendSIPMessage(dst_ip, dst_port, msg);

    // - ОСВОБОЖДЕНИЕ ВЫДЕЛЕННОЙ ПАМЯТИ -
    free(msg); free(call_id); free(from_header_full); free(from_uri_only); free(from_tag); free(via_headers); free(new_via_branch); free(to_header_full); free(to_uri_only); free(record_route_headers); free(contact_uri);
}