#ifndef RTPMANAGER_H
#define RTPMANAGER_H

#include <Arduino.h>
#include <AsyncUDP.h>
#include "ConfigManager.h"

// Предварительное объявление чтобы избежать циклической зависимости
class AudioManager;

// Структура RTP заголовка (выровненная вручную)
typedef struct {
    uint8_t first_byte;    // version(2), padding(1), extension(1), csrc_count(4)
    uint8_t second_byte;   // marker(1), payload_type(7)
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
} rtp_header_t;

#define RTP_HEADER_SIZE 12
#define RTP_PACKET_SIZE 1024
#define AUDIO_FRAME_SIZE 160

class RTPManager {
public:
    struct RTPChannel {
        bool active;
        bool rtp_socket_ready;
        AsyncUDP socket;
        char remote_ip[16];
        uint16_t remote_port;
        uint16_t local_port;
        uint32_t ssrc;
        uint16_t sequence;
        uint32_t timestamp;
        uint8_t payload_type;
        
        // Статистика
        uint32_t received_packets;
        uint32_t lost_packets;
        uint32_t jitter;
        uint16_t last_sequence;
        uint32_t last_timestamp;
        uint32_t last_packet_time;
        
        // Статистика джиттера по RFC 3550
        int32_t jitter_rfc;          // Текущий джиттер в timestamp units
        uint32_t last_rtp_timestamp; // RTP timestamp последнего пакета
        uint32_t last_arrival_time;  // Время прибытия последнего пакета (в миллисекундах)
        uint32_t clock_rate;         // Частота часов (8000 для аудио)
    };

    RTPManager();
    void init(AudioManager* audioMgr, ConfigManager* cfgMgr);
    
    bool setupChannel(int channel_id, const char* remote_ip, int remote_port, 
                     int local_port, uint32_t ssrc, uint8_t payload_type);
    void closeChannel(int channel_id);
    
    // Основные методы для аудио потока
    bool sendAudioData(int channel_id, uint8_t* audio_data, int data_len,
                      uint32_t timestamp, uint16_t sequence, uint8_t codec_type);
    void processIncomingRTPPacket(AsyncUDPPacket packet, int channel_id);
    
    void updateSync(int channel_id, uint32_t timestamp, uint16_t sequence);
    uint32_t getRandomNumber();
    void printRTPStatus();

    // Управление каналами
    int getMaxChannels() const { return max_channels; }
    bool isChannelActive(int channel_id) const;
    
    // Методы для работы с джиттером
    float getJitterMs(int channel_id) const;
    float getPacketLossPercent(int channel_id) const;
    void getCallQuality(int channel_id, float* jitter_ms, float* packet_loss_percent, 
                       uint32_t* received_packets) const;

private:
    AudioManager* audio_manager;
    ConfigManager* config_manager;
    RTPChannel* channels;
    int max_channels;
};

extern RTPManager rtpManager;

#endif