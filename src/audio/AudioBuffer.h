/*
 * AudioBuffer.h - Кольцевой буфер для обработки аудио данных
 */

#ifndef AUDIO_BUFFER_H
#define AUDIO_BUFFER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define AUDIO_BUFFER_SIZE 1024  // Размер кольцевого буфера
#define MAX_JITTER_BUFFER 20    // Максимальный размер буфера джиттера

typedef struct {
    uint16_t sequence;  // Номер пакета
    uint32_t timestamp; // Временная метка
    uint8_t data[160];  // Аудио данные (для G.711)
    size_t length;      // Длина данных
    bool valid;         // Флаг действительности
} audio_packet_t;

class AudioBuffer {
private:
    audio_packet_t buffer[MAX_JITTER_BUFFER];
    int read_index;
    int write_index;
    int count;
    SemaphoreHandle_t mutex;
    
public:
    AudioBuffer();
    ~AudioBuffer();
    
    bool writePacket(uint16_t seq, uint32_t ts, const uint8_t* data, size_t len);
    bool readPacket(uint8_t* data, size_t* len);
    bool readPacketAtSequence(uint16_t seq, uint8_t* data, size_t* len);
    void clear();
    int getCount();
    bool isFull();
    bool isEmpty();
    
    // Методы для обработки джиттера
    void setJitterLevel(int level);
    int getJitterLevel();
    void compensateJitter();
};

#endif