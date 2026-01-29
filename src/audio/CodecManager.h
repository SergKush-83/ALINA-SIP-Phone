/*
 * CodecManager.h - Управление аудио кодеками
 */

#ifndef CODEC_MANAGER_H
#define CODEC_MANAGER_H

#include <Arduino.h>

#define CODEC_PCMU 0    // μ-law
#define CODEC_PCMA 8    // A-law
#define CODEC_G729 18   // G.729
#define CODEC_OPUS 111  // Opus

typedef struct {
    uint8_t type;       // Тип кодека
    const char* name;   // Имя кодека
    int sample_rate;    // Частота дискретизации
    int frame_size;     // Размер фрейма
    int bitrate;        // Битрейт
} codec_info_t;

class CodecManager {
private:
    codec_info_t supported_codecs[4];
    uint8_t active_codec;
    
public:
    CodecManager();
    
    void init();
    uint8_t getCodecType(const char* codec_name);
    const char* getCodecName(uint8_t codec_type);
    int getSampleRate(uint8_t codec_type);
    int getFrameSize(uint8_t codec_type);
    
    // Конвертация между кодеками
    bool convertCodec(uint8_t* input, size_t input_len, uint8_t* output, size_t* output_len, 
                     uint8_t input_type, uint8_t output_type);
    
    // Кодирование/декодирование
    bool encode(uint8_t* raw_data, size_t raw_len, uint8_t* encoded_data, size_t* encoded_len, uint8_t codec_type);
    bool decode(uint8_t* encoded_data, size_t encoded_len, uint8_t* raw_data, size_t* raw_len, uint8_t codec_type);
    uint8_t ulaw_to_alaw(uint8_t ulaw);
    uint8_t alaw_to_ulaw(uint8_t alaw);
    // Установка активного кодека
    void setActiveCodec(uint8_t codec_type);
    uint8_t getActiveCodec();
    
    // Проверка поддержки кодека
    bool isCodecSupported(uint8_t codec_type);
};

#endif