/*
 * ConfigManager.h - Управление конфигурацией SIP клиента
 */

#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>

// Типы аудио кодеков
#define AUDIO_CODEC_PCMU 0    // G.711 μ-law
#define AUDIO_CODEC_PCMA 8    // G.711 A-law
//#define AUDIO_CODEC_G729 18   // G.729
//#define AUDIO_CODEC_OPUS 111  // Opus

// Структура конфигурации SIP
typedef struct {
    // Сетевые настройки
    char wifi_ssid[32];
    char wifi_password[32];
    char static_ip[16];
    char gateway[16];
    char subnet[16];
    char dns[16];
    bool dhcp_enabled;
    
    // SIP настройки
    char sip_server[64];
    int sip_port;
    char sip_username[32];
    char sip_password[32];
    char sip_display_name[32];
    char sip_realm[64];        // Authentication Realm
    int sip_expires;
    char sip_domain[64]; 
    bool sip_qop_enabled;      // Включить QOP аутентификацию <-- ДОБАВИТЬ
    
    // Аудио настройки
    int audio_sample_rate;
    int audio_frame_size;
    int audio_packet_time;
    int uart_baud_rate;
    uint8_t primary_codec;     // Основной кодек (используем uint8_t)
    uint8_t secondary_codec;   // Резервный кодек (используем uint8_t)
    bool enable_dtmf_rfc2833;
    
    // Устройство
    char device_name[32];
    uint8_t mac_address[6];
    bool dhcp_server;
    
    // Расширенные настройки
    int max_calls;
    int rtp_base_port;
    int keepalive_interval;
    bool auto_answer;
    
    // Флаги
    bool config_valid;
    uint32_t config_version;
} sip_config_t;

class ConfigManager {
private:
    Preferences preferences;
    sip_config_t current_config;
    bool config_loaded;
    
    void loadDefaults();
    bool validateConfig();
    void generateDefaultMAC();
    
public:
    ConfigManager();
    ~ConfigManager();
    
    bool loadConfig();
    bool saveConfig();
    bool resetConfig();
    
    // Геттеры
    const sip_config_t* getConfig() const;
    
    // Сетевые настройки
    const char* getWiFiSSID() const;
    const char* getWiFiPassword() const;
    const char* getStaticIP() const;
    const char* getGateway() const;
    const char* getSubnet() const;
    const char* getDNS() const;
    bool isDHCPServer() const;
    
    // SIP настройки
    const char* getSIPServer() const;
    int getSIPPort() const;
    const char* getSIPUsername() const;
    const char* getSIPPassword() const;
    const char* getSIPDisplayName() const;
    const char* getSIPRealm() const;
    int getSIPExpires() const;
    bool isQOPEnabled() const;  // <-- ДОБАВИТЬ
    
    // Аудио настройки
    int getAudioSampleRate() const;
    int getAudioFrameSize() const;
    int getAudioPacketTime() const;
    int getUARTBaudRate() const;
    uint8_t getPrimaryCodec() const;      // Возвращаем uint8_t
    uint8_t getSecondaryCodec() const;    // Возвращаем uint8_t
    bool isDTMFEnabled() const;
    
    // Устройство
    const char* getDeviceName() const;
    const uint8_t* getMACAddress() const;
    void getMACAddressString(char* mac_str) const;
    
    // Расширенные настройки
    int getMaxCalls() const;
    int getRTPBasePort() const;
    int getKeepaliveInterval() const;
    bool isAutoAnswer() const;
    
    // Сеттеры
    void setWiFiSSID(const char* ssid);
    void setWiFiPassword(const char* password);
    void setStaticIP(const char* ip);
    void setGateway(const char* gateway);
    void setSubnet(const char* subnet);
    void setDNS(const char* dns);
    void setDHCPServer(bool dhcp);
    
    void setSIPServer(const char* server);
    void setSIPPort(int port);
    void setSIPUsername(const char* username);
    void setSIPPassword(const char* password);
    void setSIPDisplayName(const char* display_name);
    void setSIPRealm(const char* realm);
    void setSIPExpires(int expires);
    void setQOPEnabled(bool enabled);  // <-- ДОБАВИТЬ
    const char* getSIPDomain() const;
    void setSIPDomain(const char* domain);
    
    void setAudioSampleRate(int rate);
    void setAudioFrameSize(int size);
    void setAudioPacketTime(int time);
    void setUARTBaudRate(int baud);
    void setPrimaryCodec(uint8_t codec);      // Принимаем uint8_t
    void setSecondaryCodec(uint8_t codec);    // Принимаем uint8_t
    void setDTMFEnabled(bool enabled);
    
    void setDeviceName(const char* name);
    bool setMACAddress(const uint8_t* mac);
    bool setMACAddressFromString(const char* mac_str);
    
    void setMaxCalls(int max_calls);
    void setRTPBasePort(int port);
    void setKeepaliveInterval(int interval);
    void setAutoAnswer(bool auto_answer);
    
    // Утилиты
    void printConfig() const;
    bool isConfigValid() const;
    uint32_t getConfigVersion() const;
};

extern ConfigManager configManager;

#endif