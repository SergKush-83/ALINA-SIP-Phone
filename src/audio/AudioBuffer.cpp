/*
 * AudioBuffer.cpp - Реализация кольцевого буфера аудио
 */

#include "AudioBuffer.h"

AudioBuffer::AudioBuffer() : read_index(0), write_index(0), count(0) {
    mutex = xSemaphoreCreateMutex();
    
    // Инициализация буфера
    for (int i = 0; i < MAX_JITTER_BUFFER; i++) {
        buffer[i].valid = false;
        buffer[i].length = 0;
    }
}

AudioBuffer::~AudioBuffer() {
    if (mutex) {
        vSemaphoreDelete(mutex);
    }
}

bool AudioBuffer::writePacket(uint16_t seq, uint32_t ts, const uint8_t* data, size_t len) {
    if (xSemaphoreTake(mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Найти место для пакета
        int target_index = -1;
        for (int i = 0; i < MAX_JITTER_BUFFER; i++) {
            if (!buffer[i].valid || buffer[i].sequence == seq) {
                target_index = i;
                break;
            }
        }
        
        if (target_index >= 0) {
            buffer[target_index].sequence = seq;
            buffer[target_index].timestamp = ts;
            buffer[target_index].length = min(len, (size_t)160);
            memcpy(buffer[target_index].data, data, buffer[target_index].length);
            buffer[target_index].valid = true;
            
            if (count < MAX_JITTER_BUFFER) {
                count++;
            }
        }
        
        xSemaphoreGive(mutex);
        return target_index >= 0;
    }
    return false;
}

bool AudioBuffer::readPacket(uint8_t* data, size_t* len) {
    if (xSemaphoreTake(mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        if (count > 0) {
            // Найти пакет с наименьшим sequence number
            int oldest_index = -1;
            uint16_t min_seq = 0xFFFF;
            
            for (int i = 0; i < MAX_JITTER_BUFFER; i++) {
                if (buffer[i].valid && buffer[i].sequence < min_seq) {
                    min_seq = buffer[i].sequence;
                    oldest_index = i;
                }
            }
            
            if (oldest_index >= 0) {
                *len = buffer[oldest_index].length;
                memcpy(data, buffer[oldest_index].data, *len);
                
                // Освободить пакет
                buffer[oldest_index].valid = false;
                count--;
                
                xSemaphoreGive(mutex);
                return true;
            }
        }
        xSemaphoreGive(mutex);
    }
    return false;
}

bool AudioBuffer::readPacketAtSequence(uint16_t seq, uint8_t* data, size_t* len) {
    if (xSemaphoreTake(mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        for (int i = 0; i < MAX_JITTER_BUFFER; i++) {
            if (buffer[i].valid && buffer[i].sequence == seq) {
                *len = buffer[i].length;
                memcpy(data, buffer[i].data, *len);
                xSemaphoreGive(mutex);
                return true;
            }
        }
        xSemaphoreGive(mutex);
    }
    return false;
}

void AudioBuffer::clear() {
    if (xSemaphoreTake(mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        for (int i = 0; i < MAX_JITTER_BUFFER; i++) {
            buffer[i].valid = false;
        }
        count = 0;
        xSemaphoreGive(mutex);
    }
}

int AudioBuffer::getCount() {
    int result = 0;
    if (xSemaphoreTake(mutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        result = count;
        xSemaphoreGive(mutex);
    }
    return result;
}

bool AudioBuffer::isFull() {
    return getCount() >= MAX_JITTER_BUFFER;
}

bool AudioBuffer::isEmpty() {
    return getCount() == 0;
}

void AudioBuffer::setJitterLevel(int level) {
    // Уровень джиттера (0-100%) - для настройки буферизации
    // В реальной реализации можно использовать для управления задержкой
}

int AudioBuffer::getJitterLevel() {
    // Возвращаем оценку уровня джиттера
    return 0; // Заглушка
}

void AudioBuffer::compensateJitter() {
    // Алгоритм компенсации джиттера
    // В реальной реализации анализирует временные метки и пакеты
}