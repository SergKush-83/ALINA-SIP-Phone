/*
 * WebInterface.cpp - Web интерфейс для настройки SIP клиента Alina
 */

#include "WebInterface.h"
#include "ConfigManager.h"
#include "SystemMonitor.h"
#include "DeviceManager.h"
#include "EnhancedSIPClient.h"
#include "RTPManager.h"

extern EnhancedSIPClient sipClient;
extern ConfigManager configManager;

WebInterface webInterface;

WebInterface::WebInterface() : server(WEB_PORT), dnsActive(false), history_count(0) {
    // Инициализация истории вызовов
    memset(call_history, 0, sizeof(call_history));
       // Инициализация каждого элемента истории
    for (int i = 0; i < MAX_CALL_HISTORY; i++) {
        call_history[i].timestamp = 0;
        memset(call_history[i].number, 0, sizeof(call_history[i].number));
        memset(call_history[i].type, 0, sizeof(call_history[i].type));
        call_history[i].duration = 0;
        call_history[i].active = false;
    } 
    // Инициализация буфера логов
    memset(log_buffer, 0, sizeof(log_buffer));
    log_buffer_pos = 0;
}

void WebInterface::handleApiCalls() {
    String json = "[";
    bool first = true;
    for (int i = 0; i < configManager.getMaxCalls(); i++) {
        // Было: if (sipClient.calls[i].state != CALL_STATE_IDLE) {
        if (sipClient.getCallState(i) != CALL_STATE_IDLE) { // ИСПРАВЛЕНО
            if (!first) json += ",";
            json += "{";
            json += "\"id\":" + String(i) + ",";
            // Было: json += "\"call_id\":\"" + String(sipClient.calls[i].call_id) + "\",";
            json += "\"call_id\":\"" + String(sipClient.getCallId(i)) + "\",";
            // Было: json += "\"remote_ip\":\"" + String(sipClient.calls[i].remote_ip) + "\",";
            json += "\"remote_ip\":\"" + String(sipClient.getRemoteIP(i)) + "\",";
            json += "\"state\":\"active\"";
            json += "}";
            first = false;
        }
    }
    json += "]";
    server.send(200, "application/json", json);
}

void WebInterface::init() {
    Serial.println("Инициализация Web интерфейса Alina");
    
    // Настройка маршрутов
    server.on("/api/calls", HTTP_GET, [this]() { this->handleApiCalls(); });
    server.on("/", HTTP_GET, [this]() { this->handleRoot(); });
    server.on("/login", HTTP_GET, [this]() { this->handleLogin(); });
    server.on("/login", HTTP_POST, [this]() { this->handleLogin(); });
    server.on("/settings", HTTP_GET, [this]() { this->handleSettings(); });
    server.on("/settings", HTTP_POST, [this]() { this->handleSaveConfig(); });
    server.on("/status", HTTP_GET, [this]() { this->handleStatus(); });
    server.on("/call", HTTP_GET, [this]() { this->handleCall(); });
    server.on("/history", HTTP_GET, [this]() { this->handleHistory(); });
    server.on("/logs", HTTP_GET, [this]() { this->handleLogs(); }); // ДОБАВИТЬ
    server.on("/api", HTTP_GET, [this]() { this->handleApi(); });
    server.on("/api/call", HTTP_GET, [this]() { this->handleApiCall(); });
    server.on("/api/call", HTTP_POST, [this]() { this->handleApiCall(); });
    server.on("/api/history", HTTP_GET, [this]() { this->handleApiHistory(); });
    server.on("/api/status", HTTP_GET, [this]() { this->handleApiStatus(); });
    server.on("/api/logs", HTTP_GET, [this]() { this->handleApiLogs(); }); // ДОБАВИТЬ
    server.on("/api/qop", HTTP_POST, [this]() { this->handleApiQOP(); });
    server.on("/reboot", HTTP_POST, [this]() { this->handleReboot(); });
    server.on("/factory_reset", HTTP_POST, [this]() { this->handleFactoryReset(); });
    server.on("/make_call", HTTP_POST, [this]() { this->handleMakeCall(); });
    server.on("/end_call", HTTP_POST, [this]() { this->handleEndCall(); });
    server.onNotFound([this]() { this->handleNotFound(); });
    server.on("/api/history/clear", HTTP_POST, [this]() { 
        this->clearCallHistory(); 
        server.send(200, "application/json", "{\"success\":true,\"message\":\"History cleared\"}");
    });
    server.on("/api/call/accept", HTTP_POST, [this]() {
        if (server.hasArg("call_id")) {
            String call_id = server.arg("call_id");
            // В текущей логике — вызов уже в состоянии WAITING_FOR_ACK
            // Просто отправляем 200 OK и ждем ACK
            // Но в новом SIP-клиенте вызов уже принят автоматически
            // Поэтому можно просто логировать
            webInterface.acceptCall(call_id.c_str());
            server.send(200, "application/json", "{\"status\":\"accepted\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"call_id_required\"}");
        }
    });

    server.on("/api/call/reject", HTTP_POST, [this]() {
        if (server.hasArg("call_id")) {
            String call_id = server.arg("call_id");
            webInterface.cancelCall(call_id.c_str());
            server.send(200, "application/json", "{\"status\":\"rejected\"}");
        } else {
            server.send(400, "application/json", "{\"error\":\"call_id_required\"}");
        }
    });
    server.begin();
    Serial.printf("Web сервер Alina запущен на порту %d\n", WEB_PORT);
}

void WebInterface::process() {
    server.handleClient();
    if (dnsActive) {
        dnsServer.processNextRequest();
    }
}

void WebInterface::handleRoot() {
    server.send(200, "text/html", getMainPage());
}

void WebInterface::handleLogin() {
    if (server.method() == HTTP_POST) {
        String username = server.arg("username");
        String password = server.arg("password");
        String action = server.arg("action");
        
        // Проверка логина/пароля
        if (username == "admin" && password == "admin") {
            if (action == "reset") {
                // Сброс настроек
                configManager.resetConfig();
                server.send(200, "text/html", 
                    "<!DOCTYPE html><html><head><title>Alina - Reset</title></head>"
                    "<body style='font-family: Arial, sans-serif; text-align: center; padding: 50px;'>"
                    "<h1>Settings Reset</h1>"
                    "<p>Configuration has been reset to factory defaults.</p>"
                    "<p><a href='/login'>Back to Login</a></p>"
                    "</body></html>");
                return;
            } else {
                server.sendHeader("Location", "/settings");
                server.send(303);
                return;
            }
        } else {
            server.send(401, "text/html", getLoginPage());
            return;
        }
    }
    
    server.send(200, "text/html", getLoginPage());
}

void WebInterface::handleSettings() {
    server.send(200, "text/html", getSettingsPage());
}

void WebInterface::handleStatus() {
    server.send(200, "text/html", getStatusPage());
}

void WebInterface::handleCall() {
    server.send(200, "text/html", getCallPage());
}

void WebInterface::handleHistory() {
    server.send(200, "text/html", getHistoryPage());
}

// Реализация методов управления вызовами
void WebInterface::cancelCall(const char* call_id) {
    cancelled_call_id = call_id;
    Serial.printf("❌ Пользователь отклонил вызов: %s\n", call_id);
    
    // Исправленный вызов addToLog
    String log_msg = "Call cancelled by user: " + String(call_id);
    addToLog(log_msg.c_str());
}

void WebInterface::acceptCall(const char* call_id) {
    accepted_call_id = call_id; // Теперь accepted_call_id объявлен
    Serial.printf("✅ Пользователь принял вызов: %s\n", call_id);
    
    // Исправленный вызов addToLog
    String log_msg = "Call accepted by user: " + String(call_id);
    addToLog(log_msg.c_str());
}

bool WebInterface::isCallCancelled(const char* call_id) {
    if (cancelled_call_id == call_id) {
        cancelled_call_id = ""; // Сбрасываем после проверки
        return true;
    }
    return false;
}

bool WebInterface::isCallAccepted(const char* call_id) {
    if (accepted_call_id == call_id) {
        accepted_call_id = ""; // Сбрасываем после проверки
        return true;
    }
    return false;
}

void WebInterface::resetCallState(const char* call_id) {
    if (cancelled_call_id == call_id) cancelled_call_id = "";
    if (accepted_call_id == call_id) accepted_call_id = "";
}

void WebInterface::handleApi() {
    String response = "{";
    response += "\"device_name\":\"" + String(configManager.getDeviceName()) + "\",";
    response += "\"ip\":\"" + String(configManager.getStaticIP()) + "\",";
    response += "\"sip_registered\":" + String(sipClient.isRegistered() ? "true" : "false") + ","; 
    response += "\"sip_state\":\"" + String(sipClient.isRegistered() ? "registered" : "not_registered") + "\",";
    response += "\"active_calls\":" + String(sipClient.getActiveCallCount()) + ",";
    response += "\"free_heap\":" + String(esp_get_free_heap_size()) + ",";
    response += "\"uptime\":" + String(millis() / 1000) + ",";
    response += "\"call_history_count\":" + String(history_count) + ",";
    // Добавляем информацию о SIP конфигурации
    response += "\"sip_server\":\"" + String(configManager.getSIPServer()) + "\",";
    response += "\"sip_port\":" + String(configManager.getSIPPort()) + ",";
    response += "\"sip_username\":\"" + String(configManager.getSIPUsername()) + "\",";
    response += "\"sip_display_name\":\"" + String(configManager.getSIPDisplayName()) + "\"";
    response += "}";
    
    server.send(200, "application/json", response);
}

void WebInterface::handleApiCall() {
    if (server.method() == HTTP_POST) {
        String action = server.arg("action");
        String number = server.arg("number");
        
        if (action == "make") {
            sipClient.makeCall(("sip:" + number + "@" + String(configManager.getSIPServer())).c_str());
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Call initiated\"}");
       } else if (action == "end") {
            if (server.hasArg("call_id")) {
                    String call_id = server.arg("call_id");
                    bool found = false;
                        for (int i = 0; i < configManager.getMaxCalls(); i++) {
                            if ((sipClient.getCallState(i) != CALL_STATE_IDLE) && call_id == sipClient.getCallId(i)) {
                                sipClient.hangupCall(i);
                                found = true;
                                break;
                            }
                        }
                        if (found) {
                                server.send(200, "application/json", "{\"success\":true,\"message\":\"Call ended\"}");
                        } else {
                                server.send(404, "application/json", "{\"success\":false,\"error\":\"Call not found\"}");
                        }
                    } else {
                    // fallback: завершить первый активный вызов
                        int id = sipClient.getFirstActiveCallId();
                        if (id >= 0) {
                                sipClient.hangupCall(id);
                            server.send(200, "application/json", "{\"success\":true,\"message\":\"Call ended\"}");
                            } else {
                            server.send(400, "application/json", "{\"success\":false,\"error\":\"No active call\"}");
                        }
                    }
                }
            }
}

void WebInterface::handleApiHistory() {
    server.send(200, "application/json", getCallHistoryJSON());
}

void WebInterface::handleApiStatus() {
    String response = "{";
    response += "\"device_name\":\"" + String(configManager.getDeviceName()) + "\",";
    response += "\"ip\":\"" + String(sipClient.getLocalIP()) + "\",";
    response += "\"mac\":\"";
    char mac_str[18];
    configManager.getMACAddressString(mac_str);
    response += String(mac_str) + "\",";
    response += "\"sip_registered\":" + String(sipClient.isRegistered() ? "true" : "false") + ",";
    response += "\"sip_state\":\"" + String(sipClient.isRegistered() ? "registered" : "not_registered") + "\",";
    response += "\"active_calls\":" + String(sipClient.getActiveCallCount()) + ",";
    response += "\"free_heap\":" + String(esp_get_free_heap_size()) + ",";
    response += "\"min_free_heap\":" + String(esp_get_minimum_free_heap_size()) + ",";
    response += "\"uptime\":" + String(millis() / 1000) + ",";
    response += "\"call_history_count\":" + String(history_count) + ",";
    // Только SIP сервер (без порта)
    response += "\"sip_server\":\"" + String(configManager.getSIPServer()) + "\"";
    response += "}";
    
    server.send(200, "application/json", response);
}

void WebInterface::handleSaveConfig() {
    // Сохранение настроек SIP
    if (server.hasArg("sip_server")) {
        configManager.setSIPServer(server.arg("sip_server").c_str());
    }
    if (server.hasArg("sip_domain")) {
        configManager.setSIPDomain(server.arg("sip_domain").c_str());
    }
    if (server.hasArg("sip_realm")) {
        configManager.setSIPRealm(server.arg("sip_realm").c_str());
    }
    if (server.hasArg("sip_port")) {
        configManager.setSIPPort(server.arg("sip_port").toInt());
    }
    if (server.hasArg("sip_username")) {
        configManager.setSIPUsername(server.arg("sip_username").c_str());
    }
    if (server.hasArg("sip_password")) {
        configManager.setSIPPassword(server.arg("sip_password").c_str());
    }
    if (server.hasArg("sip_display_name")) {
        configManager.setSIPDisplayName(server.arg("sip_display_name").c_str());
    }
    if (server.hasArg("sip_expires")) {
        configManager.setSIPExpires(server.arg("sip_expires").toInt());
    }
    configManager.setQOPEnabled(server.hasArg("sip_qop_enabled"));

    // Сохранение аудио настроек
    if (server.hasArg("primary_codec")) {
        configManager.setPrimaryCodec(server.arg("primary_codec").toInt());
    }
    if (server.hasArg("secondary_codec")) {
        configManager.setSecondaryCodec(server.arg("secondary_codec").toInt());
    }
    if (server.hasArg("audio_sample_rate")) {
        configManager.setAudioSampleRate(server.arg("audio_sample_rate").toInt());
    }
    if (server.hasArg("audio_frame_size")) {
        configManager.setAudioFrameSize(server.arg("audio_frame_size").toInt());
    }
    if (server.hasArg("audio_packet_time")) {
        configManager.setAudioPacketTime(server.arg("audio_packet_time").toInt());
    }
    if (server.hasArg("rtp_base_port")) {
        configManager.setRTPBasePort(server.arg("rtp_base_port").toInt());
    }
    if (server.hasArg("uart_baud_rate")) {
        configManager.setUARTBaudRate(server.arg("uart_baud_rate").toInt());
    }
    configManager.setDTMFEnabled(server.hasArg("dtmf_enabled"));

    // Сохранение сетевых настроек
    if (server.hasArg("static_ip")) {
        configManager.setStaticIP(server.arg("static_ip").c_str());
    }
    if (server.hasArg("gateway")) {
        configManager.setGateway(server.arg("gateway").c_str());
    }
    if (server.hasArg("subnet")) {
        configManager.setSubnet(server.arg("subnet").c_str());
    }
    if (server.hasArg("dns")) {
        configManager.setDNS(server.arg("dns").c_str());
    }
    configManager.setDHCPServer(server.hasArg("dhcp_enabled"));

    // Сохранение настроек устройства
    if (server.hasArg("device_name")) {
        configManager.setDeviceName(server.arg("device_name").c_str());
    }
    if (server.hasArg("max_calls")) {
        configManager.setMaxCalls(server.arg("max_calls").toInt());
    }
    if (server.hasArg("keepalive_interval")) {
        configManager.setKeepaliveInterval(server.arg("keepalive_interval").toInt());
    }
    configManager.setAutoAnswer(server.hasArg("auto_answer"));

    // Сохранение всех настроек в энергонезависимую память
    if (configManager.saveConfig()) {
        server.send(200, "text/plain", "Settings saved successfully");
    } else {
        server.send(500, "text/plain", "Error saving settings");
    }
}

void WebInterface::handleReboot() {
    server.send(200, "text/plain", "Rebooting...");
    delay(1000);
    ESP.restart();
}

void WebInterface::handleFactoryReset() {
    configManager.resetConfig();
    server.send(200, "text/plain", "Factory reset completed");
}

void WebInterface::handleMakeCall() {
    if (server.hasArg("number")) {
        String number = server.arg("number");
        
        // ПРАВИЛЬНЫЙ ФОРМАТ URI с использованием домена из настроек
        const char* sip_domain = configManager.getSIPDomain();
        String sip_uri;
        
        if (strlen(sip_domain) > 0) {
            sip_uri = "sip:" + number + "@" + String(sip_domain);
        } else {
            sip_uri = "sip:" + number + "@" + String(configManager.getSIPServer());
        }
        
        Serial.printf("Making call to: %s\n", sip_uri.c_str());
        webInterface.addToLog(("Making call to: " + sip_uri).c_str());
        
        sipClient.makeCall(sip_uri.c_str());
        
        // Добавляем в историю
        addCallToHistory(number.c_str(), "outgoing", 0);
        
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Call initiated\"}");
    } else {
        server.send(400, "application/json", "{\"success\":false,\"error\":\"No number provided\"}");
    }
}
void WebInterface::handleEndCall() {
    if (server.hasArg("call_id")) {
        String call_id = server.arg("call_id");
        for (int i = 0; i < configManager.getMaxCalls(); i++) {
            if ((sipClient.getCallState(i) != CALL_STATE_IDLE) && call_id == sipClient.getCallId(i)) {
                sipClient.hangupCall(i);
                server.send(200, "application/json", "{\"success\":true,\"message\":\"Call ended\"}");
                return;
            }
        }
        server.send(404, "application/json", "{\"success\":false,\"error\":\"Call not found\"}");
    } else {
        int id = sipClient.getFirstActiveCallId();
        if (id >= 0) {
            sipClient.hangupCall(id);
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Call ended\"}");
        } else {
            server.send(200, "application/json", "{\"success\":false,\"message\":\"No active call\"}");
        }
    }
}

void WebInterface::handleNotFound() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    
    for (uint8_t i = 0; i < server.args(); i++) {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    
    server.send(404, "text/plain", message);
}

String WebInterface::getLoginPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>Alina IP Phone - Login</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { width: 400px; margin: 100px auto; background: white; padding: 30px; border-radius: 10px; box-shadow: 0 0 20px rgba(0,0,0,0.1); }";
    html += "h1 { color: #0055a4; text-align: center; margin-bottom: 30px; }";
    html += "input[type='text'], input[type='password'] { width: 100%; padding: 12px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }";
    html += "input[type='submit'] { width: 100%; padding: 12px; background: #0055a4; color: white; border: none; border-radius: 5px; cursor: pointer; margin: 5px 0; }";
    html += "input[type='submit']:hover { background: #004488; }";
    html += ".reset-btn { background: #dc3545; }";
    html += ".reset-btn:hover { background: #c82333; }";
    html += ".login-info { text-align: center; margin-top: 20px; color: #666; }";
    html += "</style>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>Alina IP Phone</h1>";
    html += "<form method='post'>";
    html += "<input type='text' name='username' placeholder='Username' required><br>";
    html += "<input type='password' name='password' placeholder='Password' required><br>";
    html += "<input type='submit' value='Login'>";
    html += "<input type='submit' name='action' value='reset' class='reset-btn'>";
    html += "</form>";
    html += "<div class='login-info'>";
    html += "<p><strong>Default credentials:</strong></p>";
    html += "<p>Username: <strong>admin</strong></p>";
    html += "<p>Password: <strong>admin</strong></p>";
    html += "</div>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::getMainPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>ALINA - Main</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".btn { padding: 10px 20px; background: #0055a4; color: white; text-decoration: none; border-radius: 5px; margin: 5px; display: inline-block; }";
    html += ".btn:hover { background: #004488; }";
    html += ".stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0; }";
    html += ".stat-card { background: #f8f9fa; padding: 15px; border-radius: 5px; text-align: center; }";
    html += ".stat-value { font-size: 24px; font-weight: bold; color: #0055a4; }";
    html += ".stat-label { font-size: 14px; color: #6c757d; }";
    html += ".sip-status-registered { color: #28a745; font-weight: bold; }";
    html += ".sip-status-not-registered { color: #dc3545; font-weight: bold; }";
    html += "</style>";
    html += "<script>";
    html += "function updateStatus() {";
    html += "  fetch('/api/status').then(response => response.json()).then(data => {";
    html += "    document.getElementById('device_name').textContent = data.device_name;";
    html += "    document.getElementById('ip_address').textContent = data.ip;";
    html += "    document.getElementById('sip_status').innerHTML = data.sip_registered ? '<span class=\"sip-status-registered\">Registered</span>' : '<span class=\"sip-status-not-registered\">Not Registered</span>';";
    html += "    document.getElementById('active_calls').textContent = data.active_calls;";
    html += "    document.getElementById('memory').textContent = Math.round(data.free_heap / 1024) + ' KB';";
    html += "    document.getElementById('uptime').textContent = Math.floor(data.uptime / 60) + ' min';";
    html += "    document.getElementById('sip_server').textContent = data.sip_server;";
    html += "    document.getElementById('call_history').textContent = data.call_history_count;";
    html += "  }).catch(error => console.error('Error:', error));";
    html += "}";
    html += "setInterval(updateStatus, 5000);";
    html += "window.onload = updateStatus;";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    
    // Пиксельный логотип "ALINA" (20x5 пикселей)
    const char* logo_pixels[] = {
        "11111111111111111111", // A
        "10000000000000000001",
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"// L
    
    };
    
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";
    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    html += "<h2>Dashboard</h2>";
    html += "<div class='stats-grid'>";
    html += "<div class='stat-card'><div class='stat-value' id='device_name'>-</div><div class='stat-label'>Device Name</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='ip_address'>-</div><div class='stat-label'>IP Address</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='sip_status'>-</div><div class='stat-label'>SIP Status</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='active_calls'>-</div><div class='stat-label'>Active Calls</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='memory'>-</div><div class='stat-label'>Free Memory</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='uptime'>-</div><div class='stat-label'>Uptime</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='sip_server'>-</div><div class='stat-label'>SIP Server</div></div>";
    html += "<div class='stat-card'><div class='stat-value' id='call_history'>-</div><div class='stat-label'>Call History</div></div>";
    html += "</div>";
    
    html += "<div>";
    html += "<a href='/settings' class='btn'>Configure Settings</a>";
    html += "<a href='/status' class='btn'>View Status</a>";
    html += "<a href='/call' class='btn'>Make Call</a>";
    html += "<a href='/history' class='btn'>Call History</a>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::getSettingsPage() {
    const sip_config_t* config = configManager.getConfig();
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>ALINA - Settings</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".form-group { margin: 15px 0; }";
    html += "label { display: block; margin-bottom: 5px; font-weight: bold; }";
    html += "input[type='text'], input[type='password'], input[type='number'], select { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 5px; }";
    html += "input[type='submit'] { padding: 12px 30px; background: #0055a4; color: white; border: none; border-radius: 5px; cursor: pointer; }";
    html += "input[type='submit']:hover { background: #004488; }";
    html += ".section { margin: 30px 0; padding: 20px; border: 1px solid #ddd; border-radius: 5px; }";
    html += ".section h3 { margin-top: 0; color: #0055a4; }";
    html += ".checkbox-group { display: flex; align-items: center; }";
    html += ".checkbox-group input[type='checkbox'] { margin-right: 10px; }";
    html += ".section-btn { padding: 10px 20px; margin: 5px; background: #0055a4; color: white; border: none; border-radius: 5px; cursor: pointer; }";
    html += ".section-btn.reset { background: #cc0000; }";
    html += ".section-btn:hover { opacity: 0.8; }";
    // Стили для вкладок
    html += ".tabs { margin-bottom: 20px; }";
    html += ".tab-button { padding: 10px 20px; margin: 0 5px; background: #004488; color: white; border: none; border-radius: 5px 5px 0 0; cursor: pointer; }";
    html += ".tab-button.active { background: #0055a4; }";
    html += ".tab-button:hover { background: #003366; }";
    html += ".tab-content { display: none; }";
    html += ".tab-content.active { display: block; }";
    html += "</style>";
    html += "<script>";
    html += "function saveSettings() {";
    html += " const formData = new FormData(document.getElementById('settings_form'));";
    html += " fetch('/settings', { method: 'POST', body: formData }).then(response => {";
    html += " if (response.ok) {";
    html += " alert('Settings saved successfully!');";
    html += " window.location.reload();";
    html += " } else {";
    html += " alert('Error saving settings');";
    html += " }";
    html += " });";
    html += "}";
    html += "function openTab(evt, tabName) {";
    html += " var i, tabcontent, tabbuttons;";
    html += " tabcontent = document.getElementsByClassName('tab-content');";
    html += " for (i = 0; i < tabcontent.length; i++) {";
    html += "   tabcontent[i].classList.remove('active');";
    html += " }";
    html += " tabbuttons = document.getElementsByClassName('tab-button');";
    html += " for (i = 0; i < tabbuttons.length; i++) {";
    html += "   tabbuttons[i].classList.remove('active');";
    html += " }";
    html += " document.getElementById(tabName).classList.add('active');";
    html += " evt.currentTarget.classList.add('active');";
    html += "}";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    // Пиксельный логотип "ALINA" (20x5 пикселей)
    const char* logo_pixels[] = {
        "11111111111111111111", // A
        "10000000000000000001",
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"  // L
    };
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";
    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    html += "<form id='settings_form' method='post' action='/settings'>";

    // Вкладки
    html += "<div class='tabs'>";
    html += "<button type='button' class='tab-button active' onclick='openTab(event, \"sip\")'>SIP Settings</button>";
    html += "<button type='button' class='tab-button' onclick='openTab(event, \"network\")'>Network</button>";
    html += "<button type='button' class='tab-button' onclick='openTab(event, \"audio\")'>Audio</button>";
    html += "<button type='button' class='tab-button' onclick='openTab(event, \"device\")'>Device</button>";
    html += "</div>";

    // Содержимое вкладок
    // SIP Settings Tab
    html += "<div id='sip' class='tab-content active'>"; // Показываем по умолчанию
    html += "<div class='section'>";
    html += "<h3>SIP Settings</h3>";
    // Основные настройки
    html += "<div class='form-group'>";
    html += "<label for='sip_server'>SIP Server:</label>";
    html += "<input type='text' id='sip_server' name='sip_server' value='" + String(config->sip_server) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_domain'>SIP Domain:</label>";
    html += "<input type='text' id='sip_domain' name='sip_domain' value='" + String(config->sip_domain) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_realm'>SIP Realm:</label>";
    html += "<input type='text' id='sip_realm' name='sip_realm' value='" + String(config->sip_realm) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_port'>SIP Port:</label>";
    html += "<input type='number' id='sip_port' name='sip_port' value='" + String(config->sip_port) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_username'>Username:</label>";
    html += "<input type='text' id='sip_username' name='sip_username' value='" + String(config->sip_username) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_password'>Password:</label>";
    html += "<input type='password' id='sip_password' name='sip_password' value='" + String(config->sip_password) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_display_name'>Display Name:</label>";
    html += "<input type='text' id='sip_display_name' name='sip_display_name' value='" + String(config->sip_display_name) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='sip_expires'>Registration Expires (seconds):</label>";
    html += "<input type='number' id='sip_expires' name='sip_expires' value='" + String(config->sip_expires) + "' required>";
    html += "</div>";
    html += "<div class='form-group checkbox-group'>";
    html += "<input type='checkbox' id='sip_qop_enabled' name='sip_qop_enabled' " + String(config->sip_qop_enabled ? "checked" : "") + ">";
    html += "<label for='sip_qop_enabled'>Enable QOP (Quality of Protection)</label>";
    html += "</div>";
    html += "</div>"; // Закрытие SIP Settings
    html += "</div>"; // Закрытие tab-content SIP

    // Network Settings Tab
    html += "<div id='network' class='tab-content'>";
    html += "<div class='section'>";
    html += "<h3>Network Settings</h3>";
    html += "<div class='form-group'>";
    html += "<label for='static_ip'>Static IP:</label>";
    html += "<input type='text' id='static_ip' name='static_ip' value='" + String(config->static_ip) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='gateway'>Gateway:</label>";
    html += "<input type='text' id='gateway' name='gateway' value='" + String(config->gateway) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='subnet'>Subnet Mask:</label>";
    html += "<input type='text' id='subnet' name='subnet' value='" + String(config->subnet) + "'>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='dns'>DNS Server:</label>";
    html += "<input type='text' id='dns' name='dns' value='" + String(config->dns) + "'>";
    html += "</div>";
    html += "<div class='form-group checkbox-group'>";
    html += "<input type='checkbox' id='dhcp_enabled' name='dhcp_enabled' " + String(config->dhcp_enabled ? "checked" : "") + ">";
    html += "<label for='dhcp_enabled'>Enable DHCP Server</label>";
    html += "</div>";
    html += "</div>"; // Закрытие Network Settings
    html += "</div>"; // Закрытие tab-content Network

    // Audio Settings Tab
    html += "<div id='audio' class='tab-content'>";
    html += "<div class='section'>";
    html += "<h3>Audio Settings</h3>";
    html += "<div class='form-group'>";
    html += "<label for='primary_codec'>Primary Codec:</label>";
    html += "<select id='primary_codec' name='primary_codec'>";
    // Пример опций для кодеков, нужно адаптировать под реальные значения
    html += "<option value='0'" + String(config->primary_codec == 0 ? " selected" : "") + ">PCMU</option>";
    html += "<option value='8'" + String(config->primary_codec == 8 ? " selected" : "") + ">PCMA</option>";
    html += "<option value='9'" + String(config->primary_codec == 9 ? " selected" : "") + ">G722</option>";
    html += "<option value='18'" + String(config->primary_codec == 18 ? " selected" : "") + ">G729</option>";
    html += "</select>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='secondary_codec'>Secondary Codec:</label>";
    html += "<select id='secondary_codec' name='secondary_codec'>";
    html += "<option value='0'" + String(config->secondary_codec == 0 ? " selected" : "") + ">PCMU</option>";
    html += "<option value='8'" + String(config->secondary_codec == 8 ? " selected" : "") + ">PCMA</option>";
    html += "<option value='9'" + String(config->secondary_codec == 9 ? " selected" : "") + ">G722</option>";
    html += "<option value='18'" + String(config->secondary_codec == 18 ? " selected" : "") + ">G729</option>";
    html += "</select>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='audio_sample_rate'>Sample Rate (Hz):</label>";
    html += "<input type='number' id='audio_sample_rate' name='audio_sample_rate' value='" + String(config->audio_sample_rate) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='audio_frame_size'>Frame Size (samples):</label>";
    html += "<input type='number' id='audio_frame_size' name='audio_frame_size' value='" + String(config->audio_frame_size) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='audio_packet_time'>Packet Time (ms):</label>";
    html += "<input type='number' id='audio_packet_time' name='audio_packet_time' value='" + String(config->audio_packet_time) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='rtp_base_port'>RTP Base Port:</label>";
    html += "<input type='number' id='rtp_base_port' name='rtp_base_port' value='" + String(config->rtp_base_port) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='uart_baud_rate'>UART Baud Rate:</label>";
    html += "<input type='number' id='uart_baud_rate' name='uart_baud_rate' value='" + String(config->uart_baud_rate) + "' required>";
    html += "</div>";
    html += "<div class='form-group checkbox-group'>";
    html += "<input type='checkbox' id='dtmf_enabled' name='dtmf_enabled' " + String(config->enable_dtmf_rfc2833 ? "checked" : "") + ">";
    html += "<label for='dtmf_enabled'>Enable DTMF RFC2833</label>";
    html += "</div>";
    html += "</div>"; // Закрытие Audio Settings
    html += "</div>"; // Закрытие tab-content Audio

    // Device Settings Tab
    html += "<div id='device' class='tab-content'>";
    html += "<div class='section'>";
    html += "<h3>Device Settings</h3>";
    html += "<div class='form-group'>";
    html += "<label for='device_name'>Device Name:</label>";
    html += "<input type='text' id='device_name' name='device_name' value='" + String(config->device_name) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='max_calls'>Max Simultaneous Calls:</label>";
    html += "<input type='number' id='max_calls' name='max_calls' value='" + String(config->max_calls) + "' required>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='keepalive_interval'>Keepalive Interval (seconds):</label>";
    html += "<input type='number' id='keepalive_interval' name='keepalive_interval' value='" + String(config->keepalive_interval) + "' required>";
    html += "</div>";
    html += "<div class='form-group checkbox-group'>";
    html += "<input type='checkbox' id='auto_answer' name='auto_answer' " + String(config->auto_answer ? "checked" : "") + ">";
    html += "<label for='auto_answer'>Enable Auto Answer</label>";
    html += "</div>";
    html += "<div class='form-group'>";
    html += "<label for='mac_address'>MAC Address (Read-only):</label>";
    char mac_str[18];
    configManager.getMACAddressString(mac_str);
    html += "<input type='text' id='mac_address' name='mac_address' value='" + String(mac_str) + "' readonly>";
    html += "</div>";
    html += "</div>"; // Закрытие Device Settings
    html += "</div>"; // Закрытие tab-content Device

    html += "<input type='button' value='Save Settings' onclick='saveSettings()'>";
    html += "</form>";

    // System Controls (вне вкладок)
    html += "<div class='section'>";
    html += "<h3>System Controls</h3>";
    html += "<button class='section-btn' onclick=\"if(confirm('Reboot device?')) fetch('/reboot', {method: 'POST'}).then(() => alert('Rebooting...'))\">Reboot Device</button>";
    html += "<button class='section-btn reset' onclick=\"if(confirm('Factory reset? All settings will be lost!')) fetch('/factory_reset', {method: 'POST'}).then(() => alert('Factory reset completed'))\">Factory Reset</button>";
    html += "</div>"; // Закрытие System Controls

    html += "</div>"; // Закрытие content
    html += "</div>"; // Закрытие container
    html += "</body></html>";
    return html;
}

String WebInterface::getStatusPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>Alina IP Phone - Status</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".status-box { background: #e9ecef; padding: 15px; margin: 10px 0; border-radius: 5px; }";
    html += ".status-row { display: flex; justify-content: space-between; padding: 5px 0; border-bottom: 1px solid #ddd; }";
    html += ".status-label { font-weight: bold; }";
    html += ".refresh-btn { padding: 10px 20px; background: #0055a4; color: white; border: none; border-radius: 5px; cursor: pointer; margin: 10px 0; }";
    html += ".refresh-btn:hover { background: #004488; }";
    html += "</style>";
    html += "<script>";
    html += "function updateStatus() {";
    html += "  fetch('/api/status').then(response => response.json()).then(data => {";
    html += "    document.getElementById('device_name').textContent = data.device_name;";
    html += "    document.getElementById('ip_address').textContent = data.ip;";
    html += "    document.getElementById('mac_address').textContent = data.mac;";
    html += "    document.getElementById('sip_status').textContent = data.sip_registered ? 'Registered' : 'Not Registered';";
    html += "    document.getElementById('sip_state').textContent = data.sip_state;";
    html += "    document.getElementById('active_calls').textContent = data.active_calls;";
    html += "    document.getElementById('free_heap').textContent = Math.round(data.free_heap / 1024) + ' KB';";
    html += "    document.getElementById('min_heap').textContent = Math.round(data.min_free_heap / 1024) + ' KB';";
    html += "    document.getElementById('uptime').textContent = Math.floor(data.uptime / 60) + ' minutes';";
    html += "    document.getElementById('history_count').textContent = data.call_history_count;";
    html += "  }).catch(error => console.error('Error:', error));";
    html += "}";
    html += "window.onload = updateStatus;";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    
    // Пиксельный логотип "ALINA" (20x5 пикселей)
    const char* logo_pixels[] = {
        "11111111111111111111", // A
        "10000000000000000001",
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"// L
    
    };
    
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";
    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    
    // System Information
    html += "<div class='status-box'>";
    html += "<h3>System Information</h3>";
    html += "<div class='status-row'><span class='status-label'>Device Name:</span><span id='device_name'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>Firmware Version:</span><span>" + String(deviceManager.getFirmwareVersion()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>Uptime:</span><span id='uptime'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>Free Heap:</span><span id='free_heap'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>Min Free Heap:</span><span id='min_heap'>-</span></div>";
    html += "</div>";
    
    // Network Information
    html += "<div class='status-box'>";
    html += "<h3>Network Information</h3>";
    html += "<div class='status-row'><span class='status-label'>IP Address:</span><span id='ip_address'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>MAC Address:</span><span id='mac_address'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>Gateway:</span><span>" + String(configManager.getGateway()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>Subnet:</span><span>" + String(configManager.getSubnet()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>DNS:</span><span>" + String(configManager.getDNS()) + "</span></div>";
    html += "</div>";
    
    // SIP Status
    html += "<div class='status-box'>";
    html += "<h3>SIP Status</h3>";
    html += "<div class='status-row'><span class='status-label'>SIP Server:</span><span>" + String(configManager.getSIPServer()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>SIP Port:</span><span>" + String(configManager.getSIPPort()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>Username:</span><span>" + String(configManager.getSIPUsername()) + "</span></div>";
    html += "<div class='status-row'><span class='status-label'>Registration:</span><span id='sip_status'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>State:</span><span id='sip_state'>-</span></div>";
    html += "<div class='status-row'><span class='status-label'>Active Calls:</span><span id='active_calls'>-</span></div>";
    html += "</div>";
    
    // Call History
    html += "<div class='status-box'>";
    html += "<h3>Call History</h3>";
    html += "<div class='status-row'><span class='status-label'>Total Calls:</span><span id='history_count'>-</span></div>";
    html += "<a href='/history' class='btn' style='background: #0055a4; color: white; text-decoration: none; padding: 10px 20px; border-radius: 5px; display: inline-block;'>View History</a>";
    html += "</div>";
    
    html += "<button class='refresh-btn' onclick='updateStatus()'>Refresh Status</button>";
    
    html += "</div>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::getCallPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>Alina IP Phone - Call</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".dialpad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin: 20px 0; }";
    html += ".dialpad-btn { padding: 20px; font-size: 24px; background: #f8f9fa; border: 1px solid #ddd; border-radius: 5px; cursor: pointer; }";
    html += ".dialpad-btn:hover { background: #e9ecef; }";
    html += ".call-input { width: 100%; padding: 15px; font-size: 24px; border: 2px solid #0055a4; border-radius: 5px; margin: 20px 0; }";
    html += ".call-controls { display: flex; justify-content: center; gap: 10px; margin: 20px 0; }";
    html += ".btn-call { padding: 15px 30px; font-size: 18px; border: none; border-radius: 5px; cursor: pointer; }";
    html += ".btn-call.make { background: #28a745; color: white; }";
    html += ".btn-call.end { background: #dc3545; color: white; }";
    html += ".btn-call.mute { background: #ffc107; color: black; }";
    html += ".call-history { margin-top: 30px; }";
    html += ".history-item { padding: 10px; border-bottom: 1px solid #ddd; }";
    html += ".call-status { background: #e9ecef; padding: 15px; margin: 10px 0; border-radius: 5px; text-align: center; font-size: 18px; }";
    html += ".call-status.active { background: #d4edda; color: #155724; }";
    html += ".call-status.idle { background: #f8f9fa; color: #6c757d; }";
    html += "</style>";
    html += "<script>";
    html += "let callActive = false;";
    html += "function addToNumber(num) {";
    html += "  document.getElementById('phone_number').value += num;";
    html += "}";
    html += "function clearNumber() {";
    html += "  document.getElementById('phone_number').value = '';";
    html += "}";
    html += "function makeCall() {";
    html += "  const number = document.getElementById('phone_number').value;";
    html += "  if (number) {";
    html += "    fetch('/make_call', {";
    html += "      method: 'POST',";
    html += "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
    html += "      body: 'number=' + encodeURIComponent(number)";
    html += "    }).then(response => response.json()).then(data => {";
    html += "      if (data.success) {";
    html += "        callActive = true;";
    html += "        updateCallStatus();";
    html += "        alert('Calling ' + number);";
    html += "      } else {";
    html += "        alert('Error: ' + data.message);";
    html += "      }";
    html += "    });";
    html += "  }";
    html += "}";
    html += "function endCall() {";
    html += "  fetch('/end_call', {method: 'POST'}).then(response => response.json()).then(data => {";
    html += "    if (data.success) {";
    html += "      callActive = false;";
    html += "      updateCallStatus();";
    html += "      alert('Call ended');";
    html += "    } else {";
    html += "      alert('Error: ' + data.message);";
    html += "    }";
    html += "  });";
    html += "}";
    html += "function updateCallStatus() {";
    html += "  fetch('/api/calls').then(r => r.json()).then(calls => {";
    html += "  activeCalls = calls;";
    html += "  const statusEl = document.getElementById('call_status');";
    html += "  if (calls.length > 0) {";
    html += "        statusEl.textContent = 'Call in progress (' + calls.length + ')';";
    html += "        statusEl.className = 'call-status active';";
    html += "  } else {";
    html += "         statusEl.textContent = 'Ready to call';";
    html += "         statusEl.className = 'call-status idle';";
    html += " }";
    html += "  const incomingContainer = document.getElementById('incoming_calls');";
    html += "  if (incomingContainer) {";
    html += "    updateCallStatus();";
    html += "  incomingContainer.innerHTML = '';";
    html += "}";
    html += " }).catch(console.error);";
    html += " }";
    html += " setInterval(updateCallStatus, 2000);";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
        html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    
    // Пиксельный логотип "ALINA" (20x5 пикселей)
    const char* logo_pixels[] = {
        "11111111111111111111", // A
        "10000000000000000001",
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"// L
    
    };
    
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";
    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    
    html += "<div id='call_status' class='call-status idle'>Ready to call</div>";
    html += "<input type='text' id='phone_number' class='call-input' placeholder='Enter phone number'>";
    html += "<div class='call-controls'>";
    html += "<button class='btn-call make' onclick='makeCall()'>Call</button>";
    html += "<button class='btn-call end' onclick='endCall()'>End</button>";
    html += "<button class='btn-call mute' onclick='clearNumber()'>Clear</button>";
    html += "</div>";
    
    html += "<div class='dialpad'>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"1\")'>1</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"2\")'>2</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"3\")'>3</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"4\")'>4</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"5\")'>5</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"6\")'>6</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"7\")'>7</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"8\")'>8</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"9\")'>9</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"*\")'>*</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"0\")'>0</button>";
    html += "<button class='dialpad-btn' onclick='addToNumber(\"#\")'>#</button>";
    html += "</div>";
    
    html += "<div class='call-history'>";
    html += "<div id='incoming_calls' style='margin: 15px 0; padding: 10px; background: #fff3cd; border-radius: 5px; display: none;'>";
    html += "<strong>Incoming call from:</strong> <span id='incoming_number'>—</span>";
    html += "        <button class='btn-call make' onclick='acceptIncoming()'>Accept</button>";
    html += "        <button class='btn-call end' onclick='rejectIncoming()'>Reject</button>";
    html += "</div>";
    html += "<h3>Recent Calls</h3>";
    html += "<div class='history-item'>No recent calls</div>";
    html += "</div>";
    html += "</div>";


    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::getHistoryPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>Alina IP Phone - Call History</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".history-table { width: 100%; border-collapse: collapse; margin: 20px 0; }";
    html += ".history-table th, .history-table td { padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }";
    html += ".history-table th { background-color: #f8f9fa; font-weight: bold; }";
    html += ".history-table tr:hover { background-color: #f5f5f5; }";
    html += ".call-type { padding: 4px 8px; border-radius: 4px; font-size: 12px; }";
    html += ".incoming { background-color: #d4edda; color: #155724; }";
    html += ".outgoing { background-color: #cce5ff; color: #004085; }";
    html += ".missed { background-color: #f8d7da; color: #721c24; }";
    html += ".clear-history { padding: 10px 20px; background: #dc3545; color: white; border: none; border-radius: 5px; cursor: pointer; }";
    html += ".clear-history:hover { background: #c82333; }";
    html += "</style>";
    html += "<script>";
    html += "function loadHistory() {";
    html += "  fetch('/api/history').then(response => response.json()).then(data => {";
    html += "    const tbody = document.getElementById('history_body');";
    html += "    tbody.innerHTML = '';";
    html += "    data.forEach(call => {";
    html += "      const row = document.createElement('tr');";
    html += "      const date = new Date(call.timestamp * 1000);";
    html += "      row.innerHTML = `<td>\${date.toLocaleString()}</td><td>\${call.number}</td><td><span class='call-type \${call.type}'>\${call.type}</span></td><td>\${Math.floor(call.duration/60)}:\${(call.duration%60).toString().padStart(2,'0')}</td>`;";
    html += "      tbody.appendChild(row);";
    html += "    });";
    html += "  });";
    html += "}";
    html += "function clearHistory() {";
    html += "  if (confirm('Clear all call history?')) {";
    html += "    fetch('/api/history/clear', {";
    html += "    method: 'POST'";
    html += "   }).then(response => response.json()).then(data => {";
    html += " if (data.success) {loadHistory();}";
    html += " }).catch(error => {";
    html += " alert('Error: ' + error.message);";
    html += " });";
    html += "  }";
    html += " }";
    html += "window.onload = loadHistory;";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
        html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    
    // Пиксельный логотип "ALINA" (20x5 пикселей)
    const char* logo_pixels[] = {
        "11111111111111111111", // A
        "10000000000000000001",
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"// L
    
    };
    
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";

    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    
    html += "<button class='clear-history' onclick='clearHistory()'>Clear History</button>";
    html += "<table class='history-table'>";
    html += "<thead>";
    html += "<tr>";
    html += "<th>Date/Time</th>";
    html += "<th>Number</th>";
    html += "<th>Type</th>";
    html += "<th>Duration</th>";
    html += "</tr>";
    html += "</thead>";
    html += "<tbody id='history_body'>";
    html += "</tbody>";
    html += "</table>";
    
    html += "</div>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::getLogsPage() {
    String html = "<!DOCTYPE html>";
    html += "<html><head>";
    html += "<title>ALINA - SIP Logs</title>";
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f0f0f0; margin: 0; padding: 0; }";
    html += ".container { max-width: 1200px; margin: 0 auto; padding: 20px; }";
    html += ".header { background: #0055a4; color: white; padding: 20px; text-align: center; }";
    html += ".logo-container { margin: 0 auto; width: fit-content; }";
    html += ".logo-grid { display: grid; grid-template-columns: repeat(20, 8px); gap: 1px; margin: 0 auto; }";
    html += ".pixel { width: 8px; height: 8px; }";
    html += ".pixel-on { background-color: white; }";
    html += ".pixel-off { background-color: transparent; }";
    html += ".header-text { margin-top: 15px; }";
    html += ".header h1 { margin: 0; font-size: 36px; letter-spacing: 2px; }";
    html += ".header p { margin: 5px 0 0 0; font-size: 16px; opacity: 0.9; }";
    html += ".nav { background: #004488; padding: 10px; }";
    html += ".nav a { color: white; text-decoration: none; padding: 10px 20px; display: inline-block; }";
    html += ".nav a:hover { background: #003366; }";
    html += ".content { background: white; padding: 20px; margin-top: 20px; border-radius: 5px; }";
    html += ".logs-container { background: #1e1e1e; color: #00ff00; padding: 15px; border-radius: 5px; font-family: 'Courier New', monospace; font-size: 12px; height: 500px; overflow-y: auto; margin: 20px 0; }";
    html += ".log-entry { margin: 5px 0; }";
    html += ".log-timestamp { color: #888; }";
    html += ".log-error { color: #ff4444; }";
    html += ".log-warning { color: #ffaa00; }";
    html += ".log-success { color: #44ff44; }";
    html += ".log-info { color: #4488ff; }";
    html += ".controls { margin: 10px 0; }";
    html += ".btn { padding: 8px 16px; background: #0055a4; color: white; border: none; border-radius: 4px; cursor: pointer; margin: 5px; }";
    html += ".btn:hover { background: #004488; }";
    html += ".btn-clear { background: #dc3545; }";
    html += ".btn-clear:hover { background: #c82333; }";
    html += ".logs-container { background: #1e1e1e; color: #00ff00; padding: 15px; border-radius: 5px; font-family: 'Courier New', monospace; font-size: 12px; height: 500px; overflow-y: auto; margin: 20px 0; white-space: pre-wrap; }";
    html += ".log-entry { margin: 2px 0; }";
    html += "</style>";
    html += "<script>";
    html += "let autoRefresh = true;";
    html += "let refreshInterval;";
    html += "function updateLogs() {";
    html += "  console.log('Updating logs...');";
    html += "  fetch('/api/logs')";
    html += "    .then(response => {";
    html += "      console.log('Response status:', response.status);";
    html += "      if (!response.ok) throw new Error('HTTP ' + response.status);";
    html += "      return response.json();";
    html += "    })";
    html += "    .then(data => {";
    html += "      console.log('Data received:', data);";
    html += "      const logsContainer = document.getElementById('logs_content');";
    html += "      if (data.logs) {";
    html += "        logsContainer.innerHTML = '';";
    html += "        const lines = data.logs.split('\\\\n');";
    html += "        lines.forEach(line => {";
    html += "          if (line.trim()) {";
    html += "            const div = document.createElement('div');";
    html += "            div.className = 'log-entry';";
    html += "            div.textContent = line;";
    html += "            if (line.includes('ERROR')) {";
    html += "              div.style.color = '#ff4444';";
    html += "            } else if (line.includes('WARNING')) {";
    html += "              div.style.color = '#ffaa00';";
    html += "            } else if (line.includes('REGISTER') || line.includes('INVITE') || line.includes('ASK') || line.includes('BUY') || line.includes('CALL') || line.includes('OK')) {";
    html += "              div.style.color = '#44ff44';";
    html += "            } else {";
    html += "              div.style.color = '#4488ff';";
    html += "            }";
    html += "            logsContainer.appendChild(div);";
    html += "          }";
    html += "        });";
    html += "        logsContainer.scrollTop = logsContainer.scrollHeight;";
    html += "      }";
    html += "    })";
    html += "    .catch(error => {";
    html += "      console.error('Error:', error);";
    html += "      document.getElementById('logs_content').innerHTML = 'Error: ' + error.message;";
    html += "    });";
    html += "}";
    html += "function toggleAutoRefresh() {";
    html += "  autoRefresh = !autoRefresh;";
    html += "  const btn = document.getElementById('toggle_btn');";
    html += "  if (autoRefresh) {";
    html += "    btn.textContent = 'Pause Auto-Refresh';";
    html += "    btn.style.background = '#0055a4';";
    html += "    refreshInterval = setInterval(updateLogs, 2000);";
    html += "  } else {";
    html += "    btn.textContent = 'Resume Auto-Refresh';";
    html += "    btn.style.background = '#28a745';";
    html += "    clearInterval(refreshInterval);";
    html += "  }";
    html += "  document.getElementById('auto_refresh_status').textContent = autoRefresh ? 'ON' : 'OFF';";
    html += "}";
    html += "function clearLogs() {";
    html += "  if (confirm('Clear all logs?')) {";
    html += "    fetch('/api/logs', {";
    html += "      method: 'POST',";
    html += "      headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
    html += "      body: 'action=clear'";
    html += "    }).then(response => response.json()).then(data => {";
    html += "      if (data.success) {";
    html += "        updateLogs();";
    html += "        alert('Logs cleared successfully');";
    html += "      } else {";
    html += "        alert('Error clearing logs: ' + data.error);";
    html += "      }";
    html += "    }).catch(error => {";
    html += "      alert('Error clearing logs: ' + error.message);";
    html += "    });";
    html += "  }";
    html += "}";
    html += "function copyLogs() {";
    html += "  const logsContainer = document.getElementById('logs_content');";
    html += "  const textArea = document.createElement('textarea');";
    html += "  textArea.value = logsContainer.textContent;";
    html += "  document.body.appendChild(textArea);";
    html += "  textArea.select();";
    html += "  document.execCommand('copy');";
    html += "  document.body.removeChild(textArea);";
    html += "  alert('Logs copied to clipboard!');";
    html += "}";
    html += "function refreshNow() {";
    html += "  updateLogs();";
    html += "}";
    html += "document.addEventListener('DOMContentLoaded', function() {";
    html += "  updateLogs();";
    html += "  refreshInterval = setInterval(updateLogs, 2000);";
    html += "});";
    html += "</script>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<div class='header'>";
    html += "<div class='logo-container'>";
    html += "<div class='logo-grid'>";
    
    // Пиксельный логотип "ALINA"
    const char* logo_pixels[] = {
        "11111111111111111111",
        "10000000000000000001", 
        "10000000000000000001",
        "11111111111111111111",
        "10000000000000000001",
        "10000000000000000001"
    };
    
    for (int row = 0; row < 6; row++) {
        for (int col = 0; col < 20; col++) {
            char pixel_class[20];
            if (logo_pixels[row][col] == '1') {
                strcpy(pixel_class, "pixel pixel-on");
            } else {
                strcpy(pixel_class, "pixel pixel-off");
            }
            html += "<div class='" + String(pixel_class) + "'></div>";
        }
    }
    
    html += "</div>";
    html += "</div>";
    html += "<div class='header-text'>";
    html += "<h1>ALINA</h1>";
    html += "<p>Advanced Line for Interactive Network Audio</p>";
    html += "</div>";
    html += "</div>";
    html += "<div class='nav'>";
    html += "<a href='/'>Home</a>";
    html += "<a href='/status'>Status</a>";
    html += "<a href='/settings'>Settings</a>";
    html += "<a href='/call'>Call</a>";
    html += "<a href='/history'>History</a>";
    html += "<a href='/logs'>SIP Logs</a>";
    html += "</div>";
    html += "<div class='content'>";
    html += "<h2>SIP Logs - Real Time</h2>";
    html += "<div class='controls'>";
    html += "<button class='btn' id='toggle_btn' onclick='toggleAutoRefresh()'>Pause Auto-Refresh</button>";
    html += "<button class='btn' onclick='updateLogs()'>Refresh Now</button>";
    html += "<button class='btn' onclick='copyLogs()'>Copy Logs</button>";
    html += "<button class='btn btn-clear' onclick='clearLogs()'>Clear Logs</button>";
    html += "</div>";
    html += "<div class='logs-container' id='logs_content'>";
    html += "Loading logs...";
    html += "</div>";
    html += "<div style='font-size: 12px; color: #666;'>";
    html += "Log entries: <span id='log_count'>0</span> | ";
    html += "Last update: <span id='last_update'>-</span> | ";
    html += "Auto-refresh: <span id='auto_refresh_status'>ON</span>";
    html += "</div>";
    html += "</div>";
    html += "</div>";
    html += "</body></html>";
    
    return html;
}

String WebInterface::formatBytes(size_t bytes) {
    if (bytes < 1024) {
        return String(bytes) + " B";
    } else if (bytes < (1024 * 1024)) {
        return String(bytes / 1024.0) + " KB";
    } else if (bytes < (1024 * 1024 * 1024)) {
        return String(bytes / (1024.0 * 1024.0)) + " MB";
    } else {
        return String(bytes / (1024.0 * 1024.0 * 1024.0)) + " GB";
    }
}

String WebInterface::getCallHistoryJSON() {
    String json = "[";
    bool first = true;
    
    for (int i = 0; i < history_count; i++) {
        // Пропускаем пустые записи
        if (call_history[i].timestamp == 0) continue;
        
        if (!first) json += ",";
        json += "{";
        json += "\"timestamp\":" + String(call_history[i].timestamp) + ",";
        json += "\"number\":\"" + String(call_history[i].number) + "\",";
        json += "\"type\":\"" + String(call_history[i].type) + "\",";
        json += "\"duration\":" + String(call_history[i].duration);
        json += "}";
        first = false;
    }
    json += "]";
    
    Serial.printf("Generated JSON history: %s\n", json.c_str());
    return json;
}

void WebInterface::addCallToHistory(const char* number, const char* type, int duration) {
    // Проверяем валидность входных данных
    if (!number || !type) return;
    
    Serial.printf("Adding to history: number=%s, type=%s, duration=%d\n", number, type, duration);
    
    if (history_count < MAX_CALL_HISTORY) {
        call_history[history_count].timestamp = millis() / 1000;
        strncpy(call_history[history_count].number, number, 31);
        call_history[history_count].number[31] = '\0';
        strncpy(call_history[history_count].type, type, 15);
        call_history[history_count].type[15] = '\0';
        call_history[history_count].duration = duration;
        call_history[history_count].active = false;
        history_count++;
    } else {
        // Сдвигаем историю, если превышен лимит
        for (int i = 0; i < MAX_CALL_HISTORY - 1; i++) {
            call_history[i] = call_history[i + 1];
        }
        call_history[MAX_CALL_HISTORY - 1].timestamp = millis() / 1000;
        strncpy(call_history[MAX_CALL_HISTORY - 1].number, number, 31);
        call_history[MAX_CALL_HISTORY - 1].number[31] = '\0';
        strncpy(call_history[MAX_CALL_HISTORY - 1].type, type, 15);
        call_history[MAX_CALL_HISTORY - 1].type[15] = '\0';
        call_history[MAX_CALL_HISTORY - 1].duration = duration;
        call_history[MAX_CALL_HISTORY - 1].active = false;
    }
    
    Serial.printf("History count: %d\n", history_count);
}

String WebInterface::urlDecode(String input) {
    String decoded = "";
    for (int i = 0; i < input.length(); i++) {
        if (input.charAt(i) == '%') {
            int hex = strtol(input.substring(i + 1, i + 3).c_str(), NULL, 16);
            decoded += (char)hex;
            i += 2;
        } else if (input.charAt(i) == '+') {
            decoded += ' ';
        } else {
            decoded += input.charAt(i);
        }
    }
    return decoded;
}

String WebInterface::htmlEscape(String input) {
    input.replace("&", "&amp;");
    input.replace("<", "<");
    input.replace(">", ">");
    input.replace("\"", "&quot;");
    input.replace("'", "&#x27;");
    return input;
}

void WebInterface::handleApiQOP() {
    if (server.method() == HTTP_POST) {
        if (server.hasArg("enabled")) {
            bool enabled = server.arg("enabled") == "true";
            configManager.setQOPEnabled(enabled);
            configManager.saveConfig();
            
            // Сбрасываем аутентификацию в SIP клиенте для применения новых настроек
            sipClient.resetAuth();
            
            server.send(200, "application/json", 
                "{\"success\":true,\"message\":\"QOP " + String(enabled ? "enabled" : "disabled") + "\"}");
        } else {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Missing 'enabled' parameter\"}");
        }
    } else {
        server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    }
}

void WebInterface::handleLogs() {
    if (log_buffer_pos == 0) {
        addToLog("=== ALINA SIP LOGS STARTED ===");
        addToLog("System initialized successfully");
        addToLog("SIP client ready for registration");
        addToLog("Web interface loaded");
    }
    server.send(200, "text/html", getLogsPage());
}

void WebInterface::handleApiLogs() {
   //Serial.printf("API Logs called. Buffer pos: %d\n", log_buffer_pos);
    
    if (server.method() == HTTP_GET) {
        String response = "{";
        response += "\"logs\":\"";
        
        if (log_buffer_pos == 0) {
            // Тестовые данные без проблемных символов
            response += "No logs available\\\\n";
        } else {
            // Тщательная очистка строки
            for (int i = 0; i < log_buffer_pos; i++) {
                char c = log_buffer[i];
                // Разрешаем только печатные символы и основные управляющие
                if (c == '\n') {
                    response += "\\\\n";
                } else if (c == '\r') {
                    response += "\\\\r";
                } else if (c == '\t') {
                    response += "\\\\t";
                } else if (c == '\\') {
                    response += "\\\\\\\\";
                } else if (c == '\"') {
                    response += "\\\\\\\"";
                } else if (c >= 32 && c <= 126) {
                    // Только печатные ASCII символы
                    response += c;
                } else {
                    // Заменяем все остальные символы на пробел
                    response += ' ';
                }
            }
        }
        
        response += "\",";
        response += "\"count\":" + String(log_buffer_pos);
        response += "}";
        
        server.send(200, "application/json", response);
      //  Serial.println("Logs API response sent");
        
    } else if (server.method() == HTTP_POST) {
        if (server.hasArg("action") && server.arg("action") == "clear") {
            memset(log_buffer, 0, sizeof(log_buffer));
            log_buffer_pos = 0;
            addToLog("=== LOGS CLEARED BY USER ===");
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Logs cleared\"}");
        } else {
            server.send(400, "application/json", "{\"success\":false,\"error\":\"Invalid action\"}");
        }
    } else {
        server.send(405, "application/json", "{\"success\":false,\"error\":\"Method not allowed\"}");
    }
}

void WebInterface::addToLog(const char* message) {
    unsigned long timestamp = millis() / 1000;
    
    // Очищаем сообщение от \r символов
    String clean_message = "";
    for (int i = 0; message[i] != '\0'; i++) {
        char c = message[i];
        if (c != '\r') { // Убираем все \r символы
            clean_message += c;
        }
    }
    
    char log_entry[512];
    snprintf(log_entry, sizeof(log_entry), "[%lu] %s\n", timestamp, clean_message.c_str());
    
    int entry_len = strlen(log_entry);
    
    // Если буфер почти полон, удаляем старые записи
    if (log_buffer_pos + entry_len >= LOG_BUFFER_SIZE) {
        char* first_newline = strchr(log_buffer, '\n');
        if (first_newline) {
            int bytes_to_remove = (first_newline - log_buffer) + 1;
            memmove(log_buffer, log_buffer + bytes_to_remove, LOG_BUFFER_SIZE - bytes_to_remove);
            log_buffer_pos -= bytes_to_remove;
            memset(log_buffer + log_buffer_pos, 0, LOG_BUFFER_SIZE - log_buffer_pos);
        } else {
            memset(log_buffer, 0, sizeof(log_buffer));
            log_buffer_pos = 0;
        }
    }
    
    // Добавляем новую запись
    if (log_buffer_pos + entry_len < LOG_BUFFER_SIZE) {
        strncat(log_buffer, log_entry, LOG_BUFFER_SIZE - log_buffer_pos - 1);
        log_buffer_pos += entry_len;
    }
    
    Serial.printf("Log added: %s", log_entry);
    Serial.printf("Buffer size: %d/%d\n", log_buffer_pos, LOG_BUFFER_SIZE);
}
void WebInterface::clearCallHistory() {
    for (int i = 0; i < MAX_CALL_HISTORY; i++) {
        call_history[i].timestamp = 0;
        memset(call_history[i].number, 0, sizeof(call_history[i].number));
        memset(call_history[i].type, 0, sizeof(call_history[i].type));
        call_history[i].duration = 0;
        call_history[i].active = false;
    }
    history_count = 0;
    addToLog("Call history cleared");
}