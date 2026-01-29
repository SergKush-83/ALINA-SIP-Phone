/*
 * WebInterface.h - Web интерфейс для настройки SIP клиента ALine
 */

#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <Arduino.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "ConfigManager.h"

#define WEB_PORT 80
#define DNS_PORT 53
#define MAX_CALL_HISTORY 50

// Структура истории вызовов
typedef struct {
    uint32_t timestamp;
    char number[32];
    char type[16];  // "incoming", "outgoing", "missed"
    int duration;   // в секундах
    bool active;
} call_history_t;

class WebInterface {
private:
    WebServer server;
    DNSServer dnsServer;
    bool dnsActive;
    
    // История вызовов
    call_history_t call_history[50];
    int history_count;
    String cancelled_call_id;
    String accepted_call_id;
    // HTML страницы
    String getLoginPage();
    String getMainPage();
    String getSettingsPage();
    String getStatusPage();
    String getCallPage();
    String getHistoryPage();
    
    // API обработчики
    void handleRoot();
    void handleLogin();
    void handleSettings();
    void handleStatus();
    void handleCall();
    void handleHistory();
    void handleApi();
    void handleApiCall();
    void handleApiHistory();
    void handleApiStatus();
    void handleApiQOP();  
    void handleNotFound();
    void handleSaveConfig();
    void handleReboot();
    void handleFactoryReset();
    void handleMakeCall();
    void handleEndCall();
    
    // Вспомогательные функции
    String getHeader();
    String getFooter();
    String getNavigation();
    String formatBytes(size_t bytes);
    String getCallHistoryJSON();
    String getLogsPage();
    void handleLogs();
    void handleApiLogs();
    void clearCallHistory();

    // Буфер для хранения логов
    static const int LOG_BUFFER_SIZE = 2048;
    char log_buffer[LOG_BUFFER_SIZE];
    int log_buffer_pos;
    
public:
    WebInterface();
    void init();
    void process();
    
    // Утилиты
    static String urlDecode(String input);
    static String htmlEscape(String input);
    void addToLog(const char* message); // Метод для добавления логов
    void addCallToHistory(const char* number, const char* type, int duration);
        // Метод для отклонения вызова через веб-интерфейс
    void cancelCall(const char* call_id);
    void acceptCall(const char* call_id);
    bool isCallCancelled(const char* call_id);
    bool isCallAccepted(const char* call_id);
    void resetCallState(const char* call_id);
    void handleApiCalls();
};

extern WebInterface webInterface;

#endif