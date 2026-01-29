/*
 * CodecManager.cpp - Реализация управления аудио кодеками
 */

#include "CodecManager.h"

CodecManager::CodecManager() : active_codec(CODEC_PCMU) {
    // Инициализация поддерживаемых кодеков
    supported_codecs[0] = {CODEC_PCMU, "PCMU", 8000, 160, 64000};
    supported_codecs[1] = {CODEC_PCMA, "PCMA", 8000, 160, 64000};
    supported_codecs[2] = {CODEC_G729, "G729", 8000, 10, 8000};
    supported_codecs[3] = {CODEC_OPUS, "OPUS", 48000, 960, 64000};
}

void CodecManager::init() {
    Serial.println("Менеджер кодеков инициализирован");
}

uint8_t CodecManager::getCodecType(const char* codec_name) {
    for (int i = 0; i < 4; i++) {
        if (strcmp(supported_codecs[i].name, codec_name) == 0) {
            return supported_codecs[i].type;
        }
    }
    return 0xFF; // Неизвестный кодек
}

const char* CodecManager::getCodecName(uint8_t codec_type) {
    for (int i = 0; i < 4; i++) {
        if (supported_codecs[i].type == codec_type) {
            return supported_codecs[i].name;
        }
    }
    return "UNKNOWN";
}

int CodecManager::getSampleRate(uint8_t codec_type) {
    for (int i = 0; i < 4; i++) {
        if (supported_codecs[i].type == codec_type) {
            return supported_codecs[i].sample_rate;
        }
    }
    return 8000; // Значение по умолчанию
}

int CodecManager::getFrameSize(uint8_t codec_type) {
    for (int i = 0; i < 4; i++) {
        if (supported_codecs[i].type == codec_type) {
            return supported_codecs[i].frame_size;
        }
    }
    return 160; // Значение по умолчанию
}

bool CodecManager::convertCodec(uint8_t* input, size_t input_len, uint8_t* output, size_t* output_len, 
                               uint8_t input_type, uint8_t output_type) {
    if (input_type == output_type) {
        // Если кодеки совпадают, просто копируем
        if (input_len <= *output_len) {
            memcpy(output, input, input_len);
            *output_len = input_len;
            return true;
        }
        return false;
    }
    
    // Для простоты реализуем только G.711 конвертации
    if ((input_type == CODEC_PCMU && output_type == CODEC_PCMA) ||
        (input_type == CODEC_PCMA && output_type == CODEC_PCMU)) {
        // Конвертация между A-law и μ-law
        for (size_t i = 0; i < input_len && i < *output_len; i++) {
            if (input_type == CODEC_PCMU) {
                // μ-law to A-law
                output[i] = ulaw_to_alaw(input[i]);
            } else {
                // A-law to μ-law
                output[i] = alaw_to_ulaw(input[i]);
            }
        }
        *output_len = input_len;
        return true;
    }
    
    return false;
}

bool CodecManager::encode(uint8_t* raw_data, size_t raw_len, uint8_t* encoded_data, size_t* encoded_len, uint8_t codec_type) {
    switch (codec_type) {
        case CODEC_PCMU:
            // Простая копия для G.711 μ-law (уже закодировано)
            if (raw_len <= *encoded_len) {
                memcpy(encoded_data, raw_data, raw_len);
                *encoded_len = raw_len;
                return true;
            }
            break;
            
        case CODEC_PCMA:
            // Простая копия для G.711 A-law (уже закодировано)
            if (raw_len <= *encoded_len) {
                memcpy(encoded_data, raw_data, raw_len);
                *encoded_len = raw_len;
                return true;
            }
            break;
            
        default:
            // Для других кодеков нужно реализовать кодирование
            break;
    }
    return false;
}

bool CodecManager::decode(uint8_t* encoded_data, size_t encoded_len, uint8_t* raw_data, size_t* raw_len, uint8_t codec_type) {
    switch (codec_type) {
        case CODEC_PCMU:
        case CODEC_PCMA:
            // Простая копия для G.711 (данные уже в нужном формате)
            if (encoded_len <= *raw_len) {
                memcpy(raw_data, encoded_data, encoded_len);
                *raw_len = encoded_len;
                return true;
            }
            break;
            
        default:
            // Для других кодеков нужно реализовать декодирование
            break;
    }
    return false;
}

void CodecManager::setActiveCodec(uint8_t codec_type) {
    if (isCodecSupported(codec_type)) {
        active_codec = codec_type;
    }
}

uint8_t CodecManager::getActiveCodec() {
    return active_codec;
}

bool CodecManager::isCodecSupported(uint8_t codec_type) {
    for (int i = 0; i < 4; i++) {
        if (supported_codecs[i].type == codec_type) {
            return true;
        }
    }
    return false;
}

// Вспомогательные функции конвертации
uint8_t CodecManager::ulaw_to_alaw(uint8_t ulaw) {
    // Реализация конвертации μ-law в A-law
    // Упрощенная версия
    uint8_t sign = (ulaw & 0x80);
    uint8_t mag = (~ulaw & 0x7F);
    
    if (mag > 114) {
        mag = ((mag - 114) << 1) + 130;
    } else if (mag > 99) {
        mag = ((mag - 99) << 2) + 102;
    } else if (mag > 84) {
        mag = ((mag - 84) << 3) + 70;
    } else if (mag > 69) {
        mag = ((mag - 69) << 4) + 6;
    } else {
        mag = mag << 1;
    }
    
    return (sign ? 0xFF : 0x7F) ^ mag;
}

uint8_t CodecManager::alaw_to_ulaw(uint8_t alaw) {
    // Реализация конвертации A-law в μ-law
    // Упрощенная версия
    uint8_t sign = (alaw & 0x80);
    uint8_t mag = (alaw ^ 0x55);
    
    if (mag < 32) {
        mag = (mag << 3) + 8;
    } else if (mag < 64) {
        mag = ((mag - 32) << 2) + 8;
    } else if (mag < 96) {
        mag = ((mag - 64) << 1) + 8;
    } else {
        mag = ((mag - 96) >> 1) + 8;
    }
    
    return (sign ? 0xFF : 0x7F) ^ (mag & 0x7F);
}