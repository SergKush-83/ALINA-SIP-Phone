#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <driver/uart.h>
#include "ConfigManager.h"

class RTPManager;

// Структура UART пакета
typedef struct {
    int call_id;
    uint32_t timestamp;
    uint16_t sequence;
    uint8_t codec_type;
    uint16_t data_length;
    uint8_t* data;
} audio_packet_t;

#define UART_PORT UART_NUM_2
#define UART_BAUD_RATE 2000000
#define UART_BUFFER_SIZE 2048
#define UART_TX_PIN 17
#define UART_RX_PIN 5

#define UART_PACKET_HEADER_SIZE 14
#define UART_MAX_PACKET_SIZE (UART_PACKET_HEADER_SIZE + 1024)

// Единый clock для синхронизации timestamp
class UnifiedClock {
private:
    uint32_t base_timestamp;
    uint32_t last_update_ms;
    uint32_t samples_accumulated;
    
public:
    void init() {
        base_timestamp = 0;
        last_update_ms = millis();
        samples_accumulated = 0;
    }
    
    // Получить timestamp для исходящего пакета (20ms = 160 samples)
    uint32_t getOutgoingTimestamp() {
        uint32_t current_ts = base_timestamp + samples_accumulated;
        samples_accumulated += 160; // 20ms при 8kHz
        return current_ts;
    }
    
    // Синхронизация с входящим RTP timestamp
    void syncWithRTP(uint32_t rtp_timestamp) {
        // Корректируем base только при значительном расхождении
        // int32_t diff = (int32_t)(rtp_timestamp - (base_timestamp + samples_accumulated));
        // if (abs(diff) > 320) { // Корректируем если расхождение > 40ms
        //     base_timestamp = rtp_timestamp - samples_accumulated;
        //     Serial.printf("CLOCK SYNC: Adjusted by %d samples\n", diff);
        // }
        return;
    }
    
    // Сброс при новом вызове
    void reset() {
        samples_accumulated = 0;
        base_timestamp = millis() * 8; // Начальное значение
    }
};

class AudioManager {
public:
    AudioManager();
    void init(RTPManager* rtpMgr, ConfigManager* cfgMgr);
    void process();
    
    // Управление вызовами
    void setCallActive(int call_id, bool active);
    bool isCallActive(int call_id) const;
    void configureCall(int call_id, uint8_t codec_type, uint16_t clock_rate = 8000);
    
    // Основные аудио методы
    void processIncomingRTP(int call_id, uint8_t* rtp_data, size_t data_len, 
                           uint32_t timestamp, uint16_t sequence, uint8_t payload_type);
    
    void processOutgoingAudio(int call_id, uint8_t* audio_data, size_t data_len, 
                             uint8_t codec_type, uint32_t uart_timestamp);
    
    // Управление кодеком
    void setActiveCodec(int call_id, uint8_t codec_type);
    uint8_t getActiveCodec(int call_id);
    void resetCallAudio(int call_id);

    // Управление задачами
    void startTasks();
    void stopTasks();
    
private:
    RTPManager* rtp_manager;
    ConfigManager* config_manager;
    
    // UART
    QueueHandle_t uart_rx_queue;
    QueueHandle_t uart_tx_queue;
    uint8_t* uart_rx_buffer;
    uint8_t* uart_tx_buffer;
    uint16_t uart_packet_counter;
    
    // Задачи
    TaskHandle_t uart_task_handle;
    TaskHandle_t audio_process_task_handle;
    bool tasks_running;

    // Состояние вызовов
    struct CallState {
        bool is_active;
        uint8_t active_codec;
        uint32_t last_activity;
        uint16_t last_sequence; // Последний sequence для этого вызова
        uint32_t base_timestamp; // Базовый timestamp для вызова
        bool timestamp_initialized;
    };
    CallState* call_states;
    
    // Единые счетчики для исходящих RTP пакетов
    uint16_t global_sequence_number;
    UnifiedClock global_clock;

    // Вспомогательные методы
    bool parseUARTPacket(uint8_t* data, size_t len, audio_packet_t* packet);
    static void uartTask(void* pvParameters);
    static void audioProcessTask(void* pvParameters);
    
    void sendCallStatusToAudioKit(int call_id, bool active);
    void sendCallSettingsToAudioKit(int call_id, uint8_t codec_type, uint16_t clock_rate);
    
    // Получить следующий sequence number для вызова
    uint16_t getNextSequence(int call_id);
    
    // Получить timestamp для исходящего пакета
    uint32_t getOutgoingTimestamp(int call_id);

    // Джиттер буфер и синхронизация (для совместимости)
    struct JitterBuffer {
        void clear() { 
            // Реализация очистки джиттер-буфера
        }
        bool writePacket(uint16_t seq, uint32_t ts, uint8_t* data, size_t len) {
            return true;
        }
        bool readPacket(uint8_t* data, size_t* len) {
            return false;
        }
    };
    JitterBuffer* jitter_buffers;
    
    struct AudioSync {
        void init() {}
        void resetSync(int call_id) {}
        bool isSyncValid(int call_id) { return true; }
        void compensateJitter(int call_id) {}
        void compensateJitter(int call_id, int buffer_adjust) {}
        void updateSync(int call_id, uint32_t remote_ts, uint16_t seq) {}
    };
    AudioSync audio_sync;
};

extern AudioManager audioManager;

#endif