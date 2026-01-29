/*
 * ConfigManager.cpp - Реализация управления конфигурацией
 */

#include "ConfigManager.h"
#include <esp_random.h>

ConfigManager configManager;

ConfigManager::ConfigManager() : config_loaded(false) {
    memset(&current_config, 0, sizeof(current_config));
    loadDefaults();
}

ConfigManager::~ConfigManager() {
    preferences.end();
}

void ConfigManager::loadDefaults() {
    // Сетевые настройки по умолчанию
    strcpy(current_config.wifi_ssid, "YourWiFiSSID");
    strcpy(current_config.wifi_password, "YourWiFiPassword");
    strcpy(current_config.static_ip, "192.168.1.100");
    strcpy(current_config.gateway, "192.168.1.1");
    strcpy(current_config.subnet, "255.255.255.0");
    strcpy(current_config.dns, "8.8.8.8");
    current_config.dhcp_enabled = true;
    
    // SIP настройки по умолчанию
    strcpy(current_config.sip_server, "192.168.1.50");
    current_config.sip_port = 5060;
    strcpy(current_config.sip_username, "sipuser");
    strcpy(current_config.sip_password, "sippassword");
    strcpy(current_config.sip_display_name, "ALINA Client");
    strcpy(current_config.sip_domain, "your-domain.com"); 
    strcpy(current_config.sip_realm, "");
    strcpy(current_config.sip_domain, "your-domain.com"); // ДОБАВИТЬ
    current_config.sip_expires = 3600;
    current_config.sip_qop_enabled = false; 

    // Аудио настройки по умолчанию
    current_config.audio_sample_rate = 8000;
    current_config.audio_frame_size = 160;
    current_config.audio_packet_time = 20;
    current_config.uart_baud_rate = 2000000;
    current_config.primary_codec = AUDIO_CODEC_PCMA;      // G.711 μ-law по умолчанию
    current_config.secondary_codec = AUDIO_CODEC_PCMU;   // G.711 A-law как резерв
    current_config.enable_dtmf_rfc2833 = true;
    
    // Устройство по умолчанию
    strcpy(current_config.device_name, "ALINA IP Client");
    generateDefaultMAC();
    
    // Расширенные настройки по умолчанию
    current_config.max_calls = 5;
    current_config.rtp_base_port = 7000;
    current_config.keepalive_interval = 60000;
    current_config.auto_answer = false;
    
    // Флаги
    current_config.config_valid = true;
    current_config.config_version = 1;
}

void ConfigManager::generateDefaultMAC() {
    // Генерация случайного MAC адреса с OUI ESP32
    current_config.mac_address[0] = 0x24;
    current_config.mac_address[1] = 0x0A;
    current_config.mac_address[2] = 0xC4;
    current_config.mac_address[3] = (esp_random() >> 16) & 0xFF;
    current_config.mac_address[4] = (esp_random() >> 8) & 0xFF;
    current_config.mac_address[5] = esp_random() & 0xFF;
}

bool ConfigManager::loadConfig() {
    preferences.begin("sip_config", true); // read-only
    
    // Проверка валидности конфигурации
    current_config.config_valid = preferences.getBool("config_valid", false);
    current_config.config_version = preferences.getUInt("config_version", 0);
    
    if (!current_config.config_valid || current_config.config_version != 1) {
        Serial.println("Невалидная конфигурация, загрузка значений по умолчанию");
        loadDefaults();
        preferences.end();
        return false;
    }
    
    // Загрузка сетевых настроек
    preferences.getString("wifi_ssid", current_config.wifi_ssid, sizeof(current_config.wifi_ssid));
    preferences.getString("wifi_pass", current_config.wifi_password, sizeof(current_config.wifi_password));
    preferences.getString("static_ip", current_config.static_ip, sizeof(current_config.static_ip));
    preferences.getString("gateway", current_config.gateway, sizeof(current_config.gateway));
    preferences.getString("subnet", current_config.subnet, sizeof(current_config.subnet));
    preferences.getString("dns", current_config.dns, sizeof(current_config.dns));
    current_config.dhcp_enabled = preferences.getBool("dhcp", current_config.dhcp_enabled);
    
    // Загрузка SIP настроек
    preferences.getString("sip_server", current_config.sip_server, sizeof(current_config.sip_server));
    current_config.sip_port = preferences.getInt("sip_port", current_config.sip_port);
    preferences.getString("sip_user", current_config.sip_username, sizeof(current_config.sip_username));
    preferences.getString("sip_pass", current_config.sip_password, sizeof(current_config.sip_password));
    preferences.getString("sip_display", current_config.sip_display_name, sizeof(current_config.sip_display_name));
    preferences.getString("sip_realm", current_config.sip_realm, sizeof(current_config.sip_realm));
    preferences.getString("sip_domain", current_config.sip_domain, sizeof(current_config.sip_domain));
    current_config.sip_expires = preferences.getInt("sip_expires", current_config.sip_expires);
    current_config.sip_qop_enabled = preferences.getBool("sip_qop", current_config.sip_qop_enabled);
    preferences.getString("sip_domain", current_config.sip_domain, sizeof(current_config.sip_domain));

    // Загрузка аудио настроек
    current_config.audio_sample_rate = preferences.getInt("audio_rate", current_config.audio_sample_rate);
    current_config.audio_frame_size = preferences.getInt("audio_frame", current_config.audio_frame_size);
    current_config.audio_packet_time = preferences.getInt("audio_pkt", current_config.audio_packet_time);
    current_config.uart_baud_rate = preferences.getInt("uart_baud", current_config.uart_baud_rate);
    current_config.primary_codec = (uint8_t)preferences.getInt("primary_codec", current_config.primary_codec);
    current_config.secondary_codec = (uint8_t)preferences.getInt("secondary_codec", current_config.secondary_codec);
    current_config.enable_dtmf_rfc2833 = preferences.getBool("dtmf_enabled", current_config.enable_dtmf_rfc2833);
    
    // Загрузка настроек устройства
    preferences.getString("device_name", current_config.device_name, sizeof(current_config.device_name));
    preferences.getBytes("mac_addr", current_config.mac_address, sizeof(current_config.mac_address));
    
    // Загрузка расширенных настроек
    current_config.max_calls = preferences.getInt("max_calls", current_config.max_calls);
    current_config.rtp_base_port = preferences.getInt("rtp_port", current_config.rtp_base_port);
    current_config.keepalive_interval = preferences.getInt("keepalive", current_config.keepalive_interval);
    current_config.auto_answer = preferences.getBool("auto_answer", current_config.auto_answer);
    
    preferences.end();
    config_loaded = true;
    
    Serial.println("Конфигурация загружена");
    return true;
}

bool ConfigManager::saveConfig() {
    preferences.begin("sip_config", false); // read-write
    
    // Сохранение сетевых настроек
    preferences.putString("wifi_ssid", current_config.wifi_ssid);
    preferences.putString("wifi_pass", current_config.wifi_password);
    preferences.putString("static_ip", current_config.static_ip);
    preferences.putString("gateway", current_config.gateway);
    preferences.putString("subnet", current_config.subnet);
    preferences.putString("dns", current_config.dns);
    preferences.putBool("dhcp", current_config.dhcp_enabled);
    
    // Сохранение SIP настроек
    preferences.putString("sip_server", current_config.sip_server);
    preferences.putInt("sip_port", current_config.sip_port);
    preferences.putString("sip_user", current_config.sip_username);
    preferences.putString("sip_pass", current_config.sip_password);
    preferences.putString("sip_display", current_config.sip_display_name);
    preferences.putString("sip_realm", current_config.sip_realm);
    preferences.putString("sip_domain", current_config.sip_domain);
    preferences.putInt("sip_expires", current_config.sip_expires);
    preferences.putBool("sip_qop", current_config.sip_qop_enabled);
    preferences.putString("sip_domain", current_config.sip_domain);
    
    // Сохранение аудио настроек
    preferences.putInt("audio_rate", current_config.audio_sample_rate);
    preferences.putInt("audio_frame", current_config.audio_frame_size);
    preferences.putInt("audio_pkt", current_config.audio_packet_time);
    preferences.putInt("uart_baud", current_config.uart_baud_rate);
    preferences.putInt("primary_codec", current_config.primary_codec);
    preferences.putInt("secondary_codec", current_config.secondary_codec);
    preferences.putBool("dtmf_enabled", current_config.enable_dtmf_rfc2833);
    
    // Сохранение настроек устройства
    preferences.putString("device_name", current_config.device_name);
    preferences.putBytes("mac_addr", current_config.mac_address, sizeof(current_config.mac_address));
    
    // Сохранение расширенных настроек
    preferences.putInt("max_calls", current_config.max_calls);
    preferences.putInt("rtp_port", current_config.rtp_base_port);
    preferences.putInt("keepalive", current_config.keepalive_interval);
    preferences.putBool("auto_answer", current_config.auto_answer);
    
    // Сохранение флагов
    preferences.putBool("config_valid", current_config.config_valid);
    preferences.putUInt("config_version", current_config.config_version);
    
    preferences.end(); // Исправлено: не присваиваем результат переменной
    bool result = true; // Предполагаем успешное сохранение
    
    Serial.println("Конфигурация сохранена");
    return result;
}

bool ConfigManager::resetConfig() {
    preferences.begin("sip_config", false);
    bool result = preferences.clear();
    preferences.end();
    
    if (result) {
        loadDefaults();
        Serial.println("Конфигурация сброшена к значениям по умолчанию");
    } else {
        Serial.println("Ошибка сброса конфигурации");
    }
    
    return result;
}

// Геттеры
const sip_config_t* ConfigManager::getConfig() const {
    return &current_config;
}

const char* ConfigManager::getWiFiSSID() const {
    return current_config.wifi_ssid;
}

const char* ConfigManager::getWiFiPassword() const {
    return current_config.wifi_password;
}

const char* ConfigManager::getStaticIP() const {
    return current_config.static_ip;
}

const char* ConfigManager::getGateway() const {
    return current_config.gateway;
}

const char* ConfigManager::getSubnet() const {
    return current_config.subnet;
}

const char* ConfigManager::getDNS() const {
    return current_config.dns;
}

bool ConfigManager::isDHCPServer() const {
    return current_config.dhcp_enabled;
}

const char* ConfigManager::getSIPServer() const {
    return current_config.sip_server;
}

int ConfigManager::getSIPPort() const {
    return current_config.sip_port;
}

const char* ConfigManager::getSIPUsername() const {
    return current_config.sip_username;
}

const char* ConfigManager::getSIPPassword() const {
    return current_config.sip_password;
}

const char* ConfigManager::getSIPDisplayName() const {
    return current_config.sip_display_name;
}

const char* ConfigManager::getSIPRealm() const {
    return current_config.sip_realm;
}

int ConfigManager::getSIPExpires() const {
    return current_config.sip_expires;
}

int ConfigManager::getAudioSampleRate() const {
    return current_config.audio_sample_rate;
}

int ConfigManager::getAudioFrameSize() const {
    return current_config.audio_frame_size;
}

int ConfigManager::getAudioPacketTime() const {
    return current_config.audio_packet_time;
}

int ConfigManager::getUARTBaudRate() const {
    return current_config.uart_baud_rate;
}

uint8_t ConfigManager::getPrimaryCodec() const {  // Возвращаем uint8_t
    return current_config.primary_codec;
}

uint8_t ConfigManager::getSecondaryCodec() const {  // Возвращаем uint8_t
    return current_config.secondary_codec;
}

bool ConfigManager::isDTMFEnabled() const {
    return current_config.enable_dtmf_rfc2833;
}

const char* ConfigManager::getDeviceName() const {
    return current_config.device_name;
}

const uint8_t* ConfigManager::getMACAddress() const {
    return current_config.mac_address;
}

void ConfigManager::getMACAddressString(char* mac_str) const {
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            current_config.mac_address[0],
            current_config.mac_address[1],
            current_config.mac_address[2],
            current_config.mac_address[3],
            current_config.mac_address[4],
            current_config.mac_address[5]);
}

int ConfigManager::getMaxCalls() const {
    return current_config.max_calls;
}

int ConfigManager::getRTPBasePort() const {
    return current_config.rtp_base_port;
}

int ConfigManager::getKeepaliveInterval() const {
    return current_config.keepalive_interval;
}

bool ConfigManager::isAutoAnswer() const {
    return current_config.auto_answer;
}

// Сеттеры
void ConfigManager::setWiFiSSID(const char* ssid) {
    strncpy(current_config.wifi_ssid, ssid, sizeof(current_config.wifi_ssid) - 1);
    current_config.wifi_ssid[sizeof(current_config.wifi_ssid) - 1] = '\0';
}

void ConfigManager::setWiFiPassword(const char* password) {
    strncpy(current_config.wifi_password, password, sizeof(current_config.wifi_password) - 1);
    current_config.wifi_password[sizeof(current_config.wifi_password) - 1] = '\0';
}

void ConfigManager::setStaticIP(const char* ip) {
    strncpy(current_config.static_ip, ip, sizeof(current_config.static_ip) - 1);
    current_config.static_ip[sizeof(current_config.static_ip) - 1] = '\0';
}

void ConfigManager::setGateway(const char* gateway) {
    strncpy(current_config.gateway, gateway, sizeof(current_config.gateway) - 1);
    current_config.gateway[sizeof(current_config.gateway) - 1] = '\0';
}

void ConfigManager::setSubnet(const char* subnet) {
    strncpy(current_config.subnet, subnet, sizeof(current_config.subnet) - 1);
    current_config.subnet[sizeof(current_config.subnet) - 1] = '\0';
}

void ConfigManager::setDNS(const char* dns) {
    strncpy(current_config.dns, dns, sizeof(current_config.dns) - 1);
    current_config.dns[sizeof(current_config.dns) - 1] = '\0';
}

void ConfigManager::setDHCPServer(bool dhcp) {
    current_config.dhcp_enabled = dhcp;
}

void ConfigManager::setSIPServer(const char* server) {
    strncpy(current_config.sip_server, server, sizeof(current_config.sip_server) - 1);
    current_config.sip_server[sizeof(current_config.sip_server) - 1] = '\0';
}

void ConfigManager::setSIPPort(int port) {
    current_config.sip_port = port;
}

void ConfigManager::setSIPUsername(const char* username) {
    strncpy(current_config.sip_username, username, sizeof(current_config.sip_username) - 1);
    current_config.sip_username[sizeof(current_config.sip_username) - 1] = '\0';
}

void ConfigManager::setSIPPassword(const char* password) {
    strncpy(current_config.sip_password, password, sizeof(current_config.sip_password) - 1);
    current_config.sip_password[sizeof(current_config.sip_password) - 1] = '\0';
}

void ConfigManager::setSIPDisplayName(const char* display_name) {
    strncpy(current_config.sip_display_name, display_name, sizeof(current_config.sip_display_name) - 1);
    current_config.sip_display_name[sizeof(current_config.sip_display_name) - 1] = '\0';
}

void ConfigManager::setSIPRealm(const char* realm) {
    if (realm) {
        strncpy(current_config.sip_realm, realm, sizeof(current_config.sip_realm) - 1);
        current_config.sip_realm[sizeof(current_config.sip_realm) - 1] = '\0';
    } else {
        current_config.sip_realm[0] = '\0';
    }
}

void ConfigManager::setSIPExpires(int expires) {
    current_config.sip_expires = expires;
}

void ConfigManager::setAudioSampleRate(int rate) {
    current_config.audio_sample_rate = rate;
}

void ConfigManager::setAudioFrameSize(int size) {
    current_config.audio_frame_size = size;
}

void ConfigManager::setAudioPacketTime(int time) {
    current_config.audio_packet_time = time;
}

void ConfigManager::setUARTBaudRate(int baud) {
    current_config.uart_baud_rate = baud;
}

void ConfigManager::setPrimaryCodec(uint8_t codec) {  // Принимаем uint8_t
    current_config.primary_codec = codec;
}

void ConfigManager::setSecondaryCodec(uint8_t codec) {  // Принимаем uint8_t
    current_config.secondary_codec = codec;
}

void ConfigManager::setDTMFEnabled(bool enabled) {
    current_config.enable_dtmf_rfc2833 = enabled;
}

void ConfigManager::setDeviceName(const char* name) {
    strncpy(current_config.device_name, name, sizeof(current_config.device_name) - 1);
    current_config.device_name[sizeof(current_config.device_name) - 1] = '\0';
}

bool ConfigManager::setMACAddress(const uint8_t* mac) {
    if (!mac) return false;
    memcpy(current_config.mac_address, mac, sizeof(current_config.mac_address));
    return true;
}

bool ConfigManager::setMACAddressFromString(const char* mac_str) {
    if (!mac_str) return false;
    
    uint8_t mac[6];
    int result = sscanf(mac_str, "%02hhx:%02hhx:%02hhx:%02hhx:%02hhx:%02hhx",
                       &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
    
    if (result == 6) {
        memcpy(current_config.mac_address, mac, sizeof(current_config.mac_address));
        return true;
    }
    return false;
}

void ConfigManager::setMaxCalls(int max_calls) {
    current_config.max_calls = max_calls > 0 && max_calls <= 10 ? max_calls : 5;
}

void ConfigManager::setRTPBasePort(int port) {
    current_config.rtp_base_port = port > 1024 && port < 65535 ? port : 7000;
}

void ConfigManager::setKeepaliveInterval(int interval) {
    current_config.keepalive_interval = interval > 10000 ? interval : 60000;
}

void ConfigManager::setAutoAnswer(bool auto_answer) {
    current_config.auto_answer = auto_answer;
}

void ConfigManager::printConfig() const {
    Serial.println("=== КОНФИГУРАЦИЯ SIP КЛИЕНТА ===");
    Serial.printf("Версия конфигурации: %lu\n", (unsigned long)current_config.config_version);
    Serial.printf("Валидность: %s\n", current_config.config_valid ? "ДА" : "НЕТ");
    
    Serial.println("\n--- Сетевые настройки ---");
    Serial.printf("WiFi SSID: %s\n", current_config.wifi_ssid);
    Serial.printf("WiFi Password: %s\n", current_config.wifi_password);
    Serial.printf("Static IP: %s\n", current_config.static_ip);
    Serial.printf("Gateway: %s\n", current_config.gateway);
    Serial.printf("Subnet: %s\n", current_config.subnet);
    Serial.printf("DNS: %s\n", current_config.dns);
    Serial.printf("DHCP: %s\n", current_config.dhcp_enabled ? "ВКЛ" : "ВЫКЛ");
    
    Serial.println("\n--- SIP настройки ---");
    Serial.printf("SIP Server: %s\n", current_config.sip_server);
    Serial.printf("SIP Port: %d\n", current_config.sip_port);
    Serial.printf("SIP Username: %s\n", current_config.sip_username);
    Serial.printf("SIP Password: %s\n", current_config.sip_password);
    Serial.printf("Display Name: %s\n", current_config.sip_display_name);
    Serial.printf("Realm: %s\n", current_config.sip_realm);
    Serial.printf("Expires: %d\n", current_config.sip_expires);
    Serial.printf("QOP Enabled: %s\n", current_config.sip_qop_enabled ? "ВКЛ" : "ВЫКЛ");

    Serial.println("\n--- Аудио настройки ---");
    Serial.printf("Sample Rate: %d Hz\n", current_config.audio_sample_rate);
    Serial.printf("Frame Size: %d bytes\n", current_config.audio_frame_size);
    Serial.printf("Packet Time: %d ms\n", current_config.audio_packet_time);
    Serial.printf("UART Baud Rate: %d\n", current_config.uart_baud_rate);
    Serial.printf("Primary Codec: %d\n", current_config.primary_codec);
    Serial.printf("Secondary Codec: %d\n", current_config.secondary_codec);
    Serial.printf("DTMF RFC2833: %s\n", current_config.enable_dtmf_rfc2833 ? "ВКЛ" : "ВЫКЛ");
    
    Serial.println("\n--- Устройство ---");
    Serial.printf("Device Name: %s\n", current_config.device_name);
    char mac_str[18];
    getMACAddressString(mac_str);
    Serial.printf("MAC Address: %s\n", mac_str);
    
    Serial.println("\n--- Расширенные настройки ---");
    Serial.printf("Max Calls: %d\n", current_config.max_calls);
    Serial.printf("RTP Base Port: %d\n", current_config.rtp_base_port);
    Serial.printf("Keepalive Interval: %d ms\n", current_config.keepalive_interval);
    Serial.printf("Auto Answer: %s\n", current_config.auto_answer ? "ВКЛ" : "ВЫКЛ");
    
    Serial.println("===============================");
}

const char* ConfigManager::getSIPDomain() const {
    return current_config.sip_domain;
}

void ConfigManager::setSIPDomain(const char* domain) {
    if (domain) {
        strncpy(current_config.sip_domain, domain, sizeof(current_config.sip_domain) - 1);
        current_config.sip_domain[sizeof(current_config.sip_domain) - 1] = '\0';
    } else {
        current_config.sip_domain[0] = '\0';
    }
}

bool ConfigManager::isConfigValid() const {
    return current_config.config_valid;
}

uint32_t ConfigManager::getConfigVersion() const {
    return current_config.config_version;
}
bool ConfigManager::isQOPEnabled() const {
    return current_config.sip_qop_enabled;
}

void ConfigManager::setQOPEnabled(bool enabled) {
    current_config.sip_qop_enabled = enabled;
}