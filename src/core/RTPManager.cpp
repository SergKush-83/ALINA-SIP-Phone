/*
 * RTPManager.cpp - Оптимизированный RTP менеджер с ConfigManager
 */

#include "RTPManager.h"
#include "AudioManager.h"
#include "ConfigManager.h"

RTPManager rtpManager;

RTPManager::RTPManager() : 
    audio_manager(nullptr),
    config_manager(nullptr),
    channels(nullptr),
    max_channels(0) {
}

void RTPManager::init(AudioManager* audioMgr, ConfigManager* cfgMgr) {
    audio_manager = audioMgr;
    config_manager = cfgMgr;
    
    if (!config_manager) {
        Serial.println("RTPManager: ОШИБКА - config_manager не инициализирован");
        return;
    }
    
    // Выделение памяти для каналов на основе конфигурации
    max_channels = config_manager->getMaxCalls();
    channels = new RTPChannel[max_channels];
    
    for (int i = 0; i < max_channels; i++) {
        channels[i].active = false;
        channels[i].rtp_socket_ready = false;
        channels[i].received_packets = 0;
        channels[i].lost_packets = 0;
        channels[i].jitter = 0;
        channels[i].last_sequence = 0;
        channels[i].last_timestamp = 0;
        channels[i].last_packet_time = 0;
        
        // Инициализация джиттера по RFC 3550
        channels[i].jitter_rfc = 0;
        channels[i].last_rtp_timestamp = 0;
        channels[i].last_arrival_time = 0;
        channels[i].clock_rate = 8000; // По умолчанию 8 kHz
    }
    
    Serial.printf("RTPManager: Инициализирован для %d каналов\n", max_channels);
}

bool RTPManager::setupChannel(int channel_id, const char* remote_ip, int remote_port, 
                             int local_port, uint32_t ssrc, uint8_t payload_type) {
    if (channel_id < 0 || channel_id >= max_channels) {
        Serial.printf("RTPManager: Неверный ID канала %d (max: %d)\n", channel_id, max_channels);
        return false;
    }
    
    RTPChannel* channel = &channels[channel_id];
    
    // Настройка UDP сокета
    if (!channel->socket.listen(local_port)) {
        Serial.printf("RTPManager: Ошибка создания RTP сокета для канала %d порт %d\n", 
                     channel_id, local_port);
        return false;
    }
    
    // Настройка параметров канала
    channel->active = true;
    strncpy(channel->remote_ip, remote_ip, sizeof(channel->remote_ip) - 1);
    channel->remote_ip[sizeof(channel->remote_ip) - 1] = '\0';
    channel->remote_port = remote_port;
    channel->local_port = local_port;
    channel->ssrc = ssrc;
    channel->sequence = 0;
    channel->timestamp = 0;
    channel->payload_type = payload_type;
    channel->rtp_socket_ready = true;
    
    // Настройка параметров джиттера
    channel->jitter_rfc = 0;
    channel->last_rtp_timestamp = 0;
    channel->last_arrival_time = 0;
    
    // Определяем частоту часов в зависимости от payload type
    switch(payload_type) {
        case 0:  // PCMU
        case 8:  // PCMA
        case 9:  // G722
            channel->clock_rate = 8000;  // 8 kHz
            break;
        case 18: // G729
            channel->clock_rate = 8000;  // 8 kHz
            break;
        default:
            channel->clock_rate = 8000;  // По умолчанию 8 kHz
            break;
    }
    
    // Настройка обработчика входящих пакетов
    channel->socket.onPacket([this, channel_id](AsyncUDPPacket packet) {
        this->processIncomingRTPPacket(packet, channel_id);
    });
    
    Serial.printf("RTPManager: Канал %d настроен: %s:%d (local:%d) SSRC:%lu Clock:%dHz\n", 
                  channel_id, remote_ip, remote_port, local_port, ssrc, channel->clock_rate);
    return true;
}

// Обработка входящего RTP пакета (SIP -> RTP -> AudioManager -> UART)
void RTPManager::processIncomingRTPPacket(AsyncUDPPacket packet, int channel_id) {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) {
        return;
    }
    
    if (packet.length() < RTP_HEADER_SIZE) {
        Serial.printf("RTPManager: Слишком короткий пакет %d байт\n", packet.length());
        return;
    }
    
    // Парсинг RTP заголовка (ручная распаковка)
    uint8_t* data = packet.data();
    
    // Проверка версии RTP
    uint8_t version = (data[0] >> 6) & 0x03;
    if (version != 2) {
        Serial.printf("RTPManager: Неверная версия RTP в канале %d: %d\n", channel_id, version);
        return;
    }
    
    // Извлечение параметров
    uint16_t sequence = (data[2] << 8) | data[3];
    uint32_t timestamp = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    uint32_t ssrc = (data[8] << 24) | (data[9] << 16) | (data[10] << 8) | data[11];
    uint8_t payload_type = data[1] & 0x7F;
    
    // Извлечение аудио данных
    int payload_len = packet.length() - RTP_HEADER_SIZE;
    uint8_t* payload_data = data + RTP_HEADER_SIZE;
    
    if (payload_len > 0 && audio_manager) {
        // Отправка в AudioManager для передачи в UART
        audio_manager->processIncomingRTP(channel_id, payload_data, payload_len, 
                                         timestamp, sequence, payload_type);
        
        // Обновление статистики
        updateSync(channel_id, timestamp, sequence);
        
        // Логирование (можно отключить для производительности)
        // Serial.printf("RTP RX: Ch%d, PT%d, Seq%d, TS%lu, Len%d\n",
        //              channel_id, payload_type, sequence, timestamp, payload_len);
    }
}

// Отправка аудио данных через RTP (UART -> AudioManager -> RTP -> SIP)
bool RTPManager::sendAudioData(int channel_id, uint8_t* audio_data, int data_len,
                              uint32_t timestamp, uint16_t sequence, uint8_t codec_type) {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) {
        Serial.printf("RTPManager: Канал %d не активен\n", channel_id);
        return false;
    }

    RTPChannel* channel = &channels[channel_id];

    // Создание RTP пакета
    uint8_t rtp_packet[RTP_HEADER_SIZE + data_len];

    // Заполнение RTP заголовка (ручная упаковка)
    rtp_packet[0] = 0x80; // version=2, padding=0, extension=0, csrc_count=0
    rtp_packet[1] = codec_type & 0x7F; // marker=0, payload_type
    rtp_packet[2] = (sequence >> 8) & 0xFF;
    rtp_packet[3] = sequence & 0xFF;
    rtp_packet[4] = (timestamp >> 24) & 0xFF; // <-- ИСПОЛЬЗУЕМ ПЕРЕДАННЫЙ TIMESTAMP
    rtp_packet[5] = (timestamp >> 16) & 0xFF;
    rtp_packet[6] = (timestamp >> 8) & 0xFF;
    rtp_packet[7] = timestamp & 0xFF;
    rtp_packet[8] = (channel->ssrc >> 24) & 0xFF;
    rtp_packet[9] = (channel->ssrc >> 16) & 0xFF;
    rtp_packet[10] = (channel->ssrc >> 8) & 0xFF;
    rtp_packet[11] = channel->ssrc & 0xFF;

    // Копирование аудио данных
    memcpy(rtp_packet + RTP_HEADER_SIZE, audio_data, data_len);

    // Отправка пакета
    IPAddress remote_ip;
    if (remote_ip.fromString(channel->remote_ip)) {
        bool success = channel->socket.writeTo(rtp_packet, RTP_HEADER_SIZE + data_len,
                                             remote_ip, channel->remote_port);

        if (success) {
            // Обновление счетчиков
            // channel->sequence НЕ увеличиваем здесь, так как теперь sequence приходит извне
            // channel->timestamp НЕ увеличиваем на data_len, это неправильно.
            // channel->sequence = sequence + 1; // УБРАЛИ - sequence теперь управляется извне
            // channel->timestamp = timestamp + data_len; // УБРАЛИ - неправильное обновление

            // ВАЖНО: sequence и timestamp теперь управляются из AudioManager
            // channel->sequence и channel->timestamp в структуре RTPChannel могут стать
            // неактуальными как "ожидаемые следующие значения", но они могут использоваться
            // для других целей, например, статистики.

            // Логирование (можно отключить для производительности)
            static uint32_t last_log_time = 0;
            static uint32_t packets_sent = 0;
            packets_sent++;
            uint32_t current_time = millis();
            if (current_time - last_log_time >= 1000) {
                Serial.printf("RTP TX: Ch%d, PT%d, Seq%d, TS%lu, Len%d, Pkts/sec=%lu\n",
                             channel_id, codec_type, sequence, timestamp, data_len, packets_sent);
                packets_sent = 0;
                last_log_time = current_time;
            }

            return true;
        } else {
            Serial.printf("RTPManager: Ошибка отправки пакета в канале %d\n", channel_id);
        }
    } else {
        Serial.printf("RTPManager: Неверный IP адрес: %s\n", channel->remote_ip);
    }

    return false;
}

void RTPManager::updateSync(int channel_id, uint32_t timestamp, uint16_t sequence) {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) return;
    
    RTPChannel* channel = &channels[channel_id];
    
    // Обновление статистики
    channel->received_packets++;
    
    // Вычисление потерь пакетов
    if (channel->received_packets > 1 && sequence > channel->last_sequence + 1) {
        uint16_t lost = sequence - channel->last_sequence - 1;
        channel->lost_packets += lost;
        if (lost > 0) {
            Serial.printf("RTPManager: Потеряно %d пакетов в канале %d\n", lost, channel_id);
        }
    }
    
    // ВЫЧИСЛЕНИЕ ДЖИТТЕРА ПО RFC 3550
    if (channel->received_packets > 1) {
        // Разница во времени между пакетами в timestamp units
        int32_t D = (int32_t)(timestamp - channel->last_rtp_timestamp);
        
        // Разница во времени прибытия в миллисекундах, конвертированная в timestamp units
        uint32_t current_time = millis();
        uint32_t arrival_diff_ms = current_time - channel->last_arrival_time;
        
        // Конвертируем разницу прибытия в timestamp units (8000 Hz = 8 units/ms)
        int32_t arrival_diff_units = arrival_diff_ms * (channel->clock_rate / 1000);
        
        // Разница задержек (вариация)
        int32_t var = arrival_diff_units - D;
        
        // Абсолютное значение (для джиттера)
        if (var < 0) var = -var;
        
        // Экспоненциальное сглаживание джиттера (формула из RFC 3550)
        // J = J + (|D| - J) / 16
        channel->jitter_rfc = channel->jitter_rfc + (var - channel->jitter_rfc) / 16;
        
        // Для обратной совместимости сохраняем упрощенный джиттер
        channel->jitter = channel->jitter_rfc;
        
        // Логирование для отладки
        if (var > 1000) { // Логируем только при значительных вариациях
            Serial.printf("RTPManager: Джиттер канал %d - D=%d, arrival=%d, var=%d, jitter=%d\n",
                         channel_id, D, arrival_diff_units, var, channel->jitter_rfc);
        }
    }
    
    // Обновление временных меток
    channel->last_rtp_timestamp = timestamp;
    channel->last_arrival_time = millis();
    channel->last_sequence = sequence;
    channel->last_packet_time = millis();
}

void RTPManager::closeChannel(int channel_id) {
    if (channel_id >= 0 && channel_id < max_channels && channels[channel_id].active) {
        channels[channel_id].active = false;
        channels[channel_id].socket.close();
        channels[channel_id].rtp_socket_ready = false;
        
        Serial.printf("RTPManager: Канал %d закрыт\n", channel_id);
    }
}

bool RTPManager::isChannelActive(int channel_id) const {
    return (channel_id >= 0 && channel_id < max_channels && channels[channel_id].active);
}

// Получить текущий джиттер в миллисекундах
float RTPManager::getJitterMs(int channel_id) const {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) 
        return 0.0f;
    
    const RTPChannel* channel = &channels[channel_id];
    return (float)channel->jitter_rfc / (channel->clock_rate / 1000);
}

// Получить процент потерь пакетов
float RTPManager::getPacketLossPercent(int channel_id) const {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) 
        return 0.0f;
    
    const RTPChannel* channel = &channels[channel_id];
    if (channel->received_packets == 0) return 0.0f;
    
    return (float)channel->lost_packets / 
           (channel->received_packets + channel->lost_packets) * 100.0f;
}

// Получить статистику качества вызова
void RTPManager::getCallQuality(int channel_id, float* jitter_ms, float* packet_loss_percent, 
                       uint32_t* received_packets) const {
    if (channel_id < 0 || channel_id >= max_channels || !channels[channel_id].active) {
        if (jitter_ms) *jitter_ms = 0.0f;
        if (packet_loss_percent) *packet_loss_percent = 0.0f;
        if (received_packets) *received_packets = 0;
        return;
    }
    
    const RTPChannel* channel = &channels[channel_id];
    if (jitter_ms) *jitter_ms = getJitterMs(channel_id);
    if (packet_loss_percent) *packet_loss_percent = getPacketLossPercent(channel_id);
    if (received_packets) *received_packets = channel->received_packets;
}

uint32_t RTPManager::getRandomNumber() {
    return esp_random();
}

void RTPManager::printRTPStatus() {
    Serial.println("=== СОСТОЯНИЕ RTP ===");
    for (int i = 0; i < max_channels; i++) {
        if (channels[i].active) {
            float jitter_ms = getJitterMs(i);
            float loss_percent = getPacketLossPercent(i);
            
            Serial.printf("Канал %d: Пакеты: %lu, Потери: %lu (%.1f%%), Джиттер: %d units (%.1fms)\n",
                         i, 
                         (unsigned long)channels[i].received_packets,
                         (unsigned long)channels[i].lost_packets,
                         loss_percent,
                         channels[i].jitter_rfc,
                         jitter_ms);
        }
    }
    Serial.println("====================");
}