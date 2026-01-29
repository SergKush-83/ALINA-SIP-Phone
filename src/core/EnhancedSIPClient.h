// EnhancedSIPClient.h

#ifndef ENHANCED_SIP_CLIENT_H
#define ENHANCED_SIP_CLIENT_H

#include "Arduino.h"
#include "AsyncUDP.h"
#include "AudioManager.h" // Убедитесь, что этот файл существует
#include "RTPManager.h"   // Убедитесь, что этот файл существует
#include "EnhancedNetworkManager.h" // Убедитесь, что этот файл существует
#include "WebInterface.h" // Убедитесь, что этот файл существует
#include "ConfigManager.h" // Убедитесь, что этот файл существует

// --- Определения состояний ---
enum sip_state_t {
    SIP_STATE_INITIALIZING,
    SIP_STATE_REGISTERING,
    SIP_STATE_REGISTERED,
    SIP_STATE_ERROR
};

enum call_state_t {
    CALL_STATE_IDLE = 0,
    CALL_STATE_TRYING,
    CALL_STATE_INCOMING,
    CALL_STATE_OUTGOING, // <-- Добавлено
    CALL_STATE_RINGING,
    CALL_STATE_ACTIVE,
    CALL_STATE_WAITING_FOR_ACK,
    CALL_STATE_TERMINATED,
    CALL_STATE_INVITE_SENT // <-- Добавлено
};

// --- Определения вызова ---
//#define MAX_CALLS 3
#define CALL_ID_LEN 64
#define TAG_LEN 64
#define URI_LEN 128
#define IP_LEN 16
#define MAX_SIP_CREDENTIALS_LEN 64
#define MAX_SIP_SERVER_LEN 64
#define MAX_SIP_PASSWORD_LEN 64
#define MAX_SIP_USER_LEN 32
#define SIP_PORT 5060
// --- Добавлены определения ---
#define RECORD_ROUTE_LEN 256 // <-- Добавлено
#define MAX_QOP_VALUE_LEN 16 // <-- Добавлено, если нужно для auth_info_t
// ---

// Структура для хранения информации о вызове
typedef struct {
    int id;
    call_state_t state;
    char call_id[CALL_ID_LEN];
    char from_uri[URI_LEN];
    char from_tag[TAG_LEN];
    char to_uri[URI_LEN];
    char to_tag[TAG_LEN];
    char remote_ip[IP_LEN];
    uint16_t remote_sip_port;
    uint16_t remote_rtp_port;
    uint16_t local_rtp_port;
    uint32_t cseq_invite; // Для сопоставления INVITE и 200 OK / ACK
    uint32_t ack_cseq;    // Для сопоставления ACK с 200 OK
    uint32_t bye_cseq;    // Для сопоставления BYE с ответом
    uint32_t cancel_cseq; // Для сопоставления CANCEL с INVITE
    char contact_uri[URI_LEN]; // URI из заголовка Contact для отправки ACK, BYE
    unsigned long last_activity;

    // --- Добавлены поля ---
    char record_route[RECORD_ROUTE_LEN]; // <-- Добавлено для хранения Record-Route
    uint32_t ssrc;                       // <-- Добавлено для RTP SSRC
    // ---
} call_t;

// Структура для хранения информации об аутентификации
typedef struct {
    char realm[64];
    char nonce[64];
    char opaque[64];
    char algorithm[16];
    char qop[MAX_QOP_VALUE_LEN]; // Например, "auth" или "auth-int" (обычно "auth")
    bool stale; // Может быть, если сервер присылает stale=true
    bool requires_proxy_auth; // Флаг: true если последний 407 был для прокси
    char proxy_nonce[128];    // Отдельные поля для proxy-аутентификации (альтернатива флагу)
    char proxy_realm[128];    // Но проще использовать одни и те же поля и флаг
    // УБРАНО: char algorithm[16]; // MD5, SHA-256, etc. (обычно MD5)
} auth_info_t;

class EnhancedSIPClient {
public:
    EnhancedSIPClient();
    ~EnhancedSIPClient();
    // Инициализация с зависимостями
    void init(EnhancedNetworkManager* netMgr, AudioManager* audioMgr, RTPManager* rtpMgr, WebInterface* webInt, ConfigManager* cfgMgr);

    // Основной цикл обработки
    void process();

    // Установка учётных данных SIP
    void setSIPCredentials(const char* user, const char* password, const char* server, uint16_t port);

    // Проверка регистрации
    bool isRegistered() const { return sip_registered; }

    // Состояние SIP
    sip_state_t getState() const { return sip_state; }

    // Начать вызов
    void makeCall(const char* to_uri);

    // Завершить вызов
    void hangupCall(int call_id);

    // --- ГЕТТЕРЫ для WebInterface ---
    call_state_t getCallState(int call_index) const;
    const char* getCallId(int call_index) const;
    const char* getRemoteIP(int call_index) const;
    const char* getFromUri(int call_index) const;
    int getFirstActiveCallId() const;
    int getActiveCallCount() const { return active_calls; }
    // ---

    // Сброс аутентификации (например, при изменении настроек)
    void resetAuth();
    void sendResponse(int code, const char* reason, const char* dst_ip, uint16_t dst_port,
                     const char* request, const char* to_tag, bool with_sdp, uint16_t local_rtp_port);

    void sendRinging(const char* request, const char* dst_ip, uint16_t dst_port, const char* to_tag = nullptr);   
    void sendTrying(const char* request, const char* dst_ip, uint16_t dst_port);

    // Публичный метод для получения локального IP через networkManager
    const char* getLocalIP(); // <-- Сделан публичным

private:
    // --- Указатели на зависимости ---
    EnhancedNetworkManager* networkManager;
    AudioManager* audioManager;
    RTPManager* rtpManager;
    WebInterface* webInterface;
    ConfigManager* configManager;

    // --- Состояния и данные ---
    sip_state_t sip_state;
    bool sip_registered;
    unsigned long last_register_success;
    int register_expires;
    bool require_auth = false; // Флаг, указывающий, что требуется аутентификация
    int active_calls;
    int max_calls;
    bool audio_tasks_started;
    // --- НОВОЕ ---
    bool require_proxy_auth = false; // Отдельный флаг для proxy auth
    char response_buffer[1024];
    char sdp_buffer[384];
    char via_buffer[256];
    char temp_buffers[4][128]; // Для временных данных
    // --- Учётные данные ---
    char sip_user[MAX_SIP_USER_LEN];
    char sip_password[MAX_SIP_PASSWORD_LEN];
    char sip_server[MAX_SIP_SERVER_LEN];
    uint16_t sip_server_port;
    char call_id[CALL_ID_LEN]; // Уникальный Call-ID для сессии

    // --- Информация об аутентификации ---
    auth_info_t auth_info;

    // --- Вызовы ---
    call_t* calls;
    uint32_t sip_cseq; // Общий CSeq для запросов

    // --- Внутренние методы ---
    void handleRegistration(bool is_retry_after_401 = false); // Изменённый метод
    void handleIncomingPacket(AsyncUDPPacket& packet); // <-- Изменено объявление, принимает &
    void handleIncomingRequest(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handleIncomingResponse(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handleIncomingINVITE(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handleIncomingACK(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handleIncomingBYE(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handleIncomingCANCEL(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void handle200OK(const char* data, size_t len, const char* remote_ip, uint16_t remote_port);
    void sendSIPMessage(const char* ip, uint16_t port, const char* msg);
    
    void sendACK(call_t* call);
    void sendBYE(call_t* call);

    bool extractSIPHeader(const char* data, size_t len, const char* header, char* output, size_t out_size, const char* sub = nullptr);
    void parseAuthHeader(const char* header, auth_info_t* auth);
    // УДАЛЕН calculateResponse - логика теперь внутри handleRegistration
    int findFreeCallSlot();
    void resetCall(call_t* call);
    void parseContactURI(const char* contact_uri, char* ip, uint16_t* port);
    // getLocalIP теперь публичный метод
    bool validateNetwork() const;
    bool validateSIPCredentials() const;
    bool extractFirstViaHeader(const char* data, size_t len, char* output, size_t out_size);
    void generateSDPBody(char* buffer, size_t buffer_size, const char* local_ip, uint16_t local_rtp_port);
};

extern EnhancedSIPClient sipClient;


#endif // ENHANCED_SIP_CLIENT_H