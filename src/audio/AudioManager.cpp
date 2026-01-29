#include "AudioManager.h"
#include "ConfigManager.h"
#include "RTPManager.h"

AudioManager audioManager;

AudioManager::AudioManager() : 
    rtp_manager(nullptr),
    config_manager(nullptr),
    uart_rx_queue(nullptr),
    uart_tx_queue(nullptr),
    uart_rx_buffer(nullptr),
    uart_tx_buffer(nullptr),
    uart_packet_counter(0),
    uart_task_handle(nullptr),
    audio_process_task_handle(nullptr),
    tasks_running(false),
    call_states(nullptr),
    global_sequence_number(0),
    jitter_buffers(nullptr) {
}

void AudioManager::init(RTPManager* rtpMgr, ConfigManager* cfgMgr) {
    rtp_manager = rtpMgr;
    config_manager = cfgMgr;
    
    if (!config_manager) {
        Serial.println("AudioManager: ОШИБКА - config_manager не инициализирован");
        return;
    }

    // Инициализация глобального clock
    global_clock.init();

    // Выделение памяти для состояний вызовов
    int max_calls = config_manager->getMaxCalls();
    call_states = new CallState[max_calls];
    jitter_buffers = new JitterBuffer[max_calls];
    
    for (int i = 0; i < max_calls; i++) {
        call_states[i].is_active = false;
        call_states[i].active_codec = config_manager->getPrimaryCodec();
        call_states[i].last_activity = 0;
        call_states[i].last_sequence = 0;
        call_states[i].base_timestamp = 0;
        call_states[i].timestamp_initialized = false;
    }
    
    // Настройка UART
    uart_config_t uart_config = {
        .baud_rate = config_manager->getUARTBaudRate(),
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_APB,
    };
    
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT, UART_BUFFER_SIZE * 4, UART_BUFFER_SIZE * 4, 0, NULL, 0);
    
    // Выделение буферов
    uart_rx_buffer = (uint8_t*)heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_DMA);
    uart_tx_buffer = (uint8_t*)heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_DMA);
    
    if (!uart_rx_buffer || !uart_tx_buffer) {
        Serial.println("Ошибка выделения буферов UART");
        return;
    }
    
    // Создание очередей
    uart_rx_queue = xQueueCreate(20, sizeof(audio_packet_t));
    uart_tx_queue = xQueueCreate(20, sizeof(audio_packet_t));
    
    Serial.printf("AudioManager: Инициализирован для %d вызовов\n", max_calls);
}

uint16_t AudioManager::getNextSequence(int call_id) {
    if (call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        // УВЕЛИЧИВАЕМ НА 1 КАЖДЫЙ РАЗ
        call_states[call_id].last_sequence++;
        Serial.printf("AudioManager: Seq=%d for call%d\n", 
                     call_states[call_id].last_sequence, call_id);
        return call_states[call_id].last_sequence;
    }
    return global_sequence_number++;
}

uint32_t AudioManager::getOutgoingTimestamp(int call_id) {
    // Используем единый clock для всех вызовов для синхронизации
    return global_clock.getOutgoingTimestamp();
}

// Обработка входящего RTP пакета от SIP -> отправка в UART
void AudioManager::processIncomingRTP(int call_id, uint8_t* rtp_data, size_t data_len, 
                                     uint32_t timestamp, uint16_t sequence, uint8_t payload_type) {
    if (!config_manager || call_id < 0 || call_id >= config_manager->getMaxCalls() || 
        !rtp_data || data_len == 0) return;
    
    // Автоматическая активация вызова при получении RTP
    if (!isCallActive(call_id)) {
        setCallActive(call_id, true);
        configureCall(call_id, config_manager->getPrimaryCodec(), 8000);
        Serial.printf("AudioManager: AUTO-ACTIVATED Call%d on first RTP packet\n", call_id);
    }
    
    // Синхронизация clock с входящим RTP
    //global_clock.syncWithRTP(timestamp);
    
    // Формирование UART пакета
    size_t packet_size = UART_PACKET_HEADER_SIZE + data_len;
    uint8_t* uart_packet = (uint8_t*)malloc(packet_size);
    if (!uart_packet) return;
    
    // Заполнение заголовка
    uart_packet[0] = 0x55;
    uart_packet[1] = 0xAA;
    uart_packet[2] = (uart_packet_counter >> 8) & 0xFF;
    uart_packet[3] = uart_packet_counter & 0xFF;
    uart_packet[4] = (data_len >> 8) & 0xFF;
    uart_packet[5] = data_len & 0xFF;
    uart_packet[6] = getActiveCodec(call_id);
    
    // Используем timestamp из RTP пакета для обратной связи
    uart_packet[7] = (timestamp >> 24) & 0xFF;
    uart_packet[8] = (timestamp >> 16) & 0xFF;
    uart_packet[9] = (timestamp >> 8) & 0xFF;
    uart_packet[10] = timestamp & 0xFF;
    
    uart_packet[11] = (sequence >> 8) & 0xFF;
    uart_packet[12] = sequence & 0xFF;
    uart_packet[13] = call_id;
    
    memcpy(uart_packet + UART_PACKET_HEADER_SIZE, rtp_data, data_len);
    
    // Отправка по UART
    uart_write_bytes(UART_PORT, (const char*)uart_packet, packet_size);
    
    free(uart_packet);
    
    // Обновление активности
    call_states[call_id].last_activity = millis();
    call_states[call_id].is_active = true;
    
    uart_packet_counter++;
    
    // Логирование
    static uint32_t last_log = 0;
    if (millis() - last_log > 1000) {
        Serial.printf("RTP->UART: Call%d, Seq%d, TS%lu, Len%d\n", 
                     call_id, sequence, timestamp, data_len);
        last_log = millis();
    }
}

// Обработка исходящих данных от UART -> отправка в RTP
void AudioManager::processOutgoingAudio(int call_id, uint8_t* audio_data, size_t data_len, 
                                       uint8_t codec_type, uint32_t uart_timestamp) {
    if (!isCallActive(call_id)) return;

    // ИГНОРИРУЕМ uart_timestamp от AudioKit (он всегда 0)
    // Генерируем свои последовательные timestamp и sequence
    
    uint16_t sequence = getNextSequence(call_id);
    uint32_t timestamp = getOutgoingTimestamp(call_id);

    // Отправка в RTP
    rtp_manager->sendAudioData(call_id, audio_data, data_len,
                              timestamp, sequence, codec_type);

    // Логирование
    static uint32_t last_log = 0;
    if (millis() - last_log > 1000) {
        Serial.printf("UART->RTP: Call%d, TS%lu, Seq%d, Len%d\n",
                     call_id, timestamp, sequence, data_len);
        last_log = millis();
    }

    call_states[call_id].last_activity = millis();
}

bool AudioManager::parseUARTPacket(uint8_t* data, size_t len, audio_packet_t* packet) {
    if (len < UART_PACKET_HEADER_SIZE) return false;
    
    if (data[0] != 0x55 || data[1] != 0xAA) {
        return false;
    }
    
    uint16_t data_length = (data[4] << 8) | data[5];
    
    if (data_length > (len - UART_PACKET_HEADER_SIZE)) {
        Serial.printf("AudioManager: Неверная длина данных %d > %d\n", 
                     data_length, (len - UART_PACKET_HEADER_SIZE));
        return false;
    }
    
    packet->call_id = data[13];
    packet->timestamp = (data[7] << 24) | (data[8] << 16) | (data[9] << 8) | data[10];
    packet->sequence = (data[11] << 8) | data[12];
    packet->codec_type = data[6];
    packet->data_length = data_length;
    
    if (packet->data_length > 0) {
        packet->data = (uint8_t*)malloc(packet->data_length);
        if (!packet->data) return false;
        memcpy(packet->data, data + UART_PACKET_HEADER_SIZE, packet->data_length);
    }
    
    return true;
}

void AudioManager::uartTask(void* pvParameters) {
    AudioManager* audioMgr = (AudioManager*)pvParameters;
    uint8_t rx_buffer[UART_MAX_PACKET_SIZE];
    size_t rx_index = 0;
    bool in_packet = false;
    uint16_t expected_length = 0;
    
    Serial.println("AudioManager: Задача UART запущена");
    
    while (1) {
        int len = uart_read_bytes(UART_PORT, audioMgr->uart_rx_buffer, 
                                 UART_BUFFER_SIZE, 20 / portTICK_PERIOD_MS);
        
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                uint8_t byte = audioMgr->uart_rx_buffer[i];
                
                if (!in_packet) {
                    if (rx_index == 0 && byte == 0x55) {
                        rx_buffer[rx_index++] = byte;
                    } else if (rx_index == 1 && byte == 0xAA) {
                        rx_buffer[rx_index++] = byte;
                        in_packet = true;
                        expected_length = 0;
                    } else {
                        rx_index = 0;
                    }
                } else {
                    if (rx_index < UART_MAX_PACKET_SIZE) {
                        rx_buffer[rx_index++] = byte;
                        
                        if (rx_index == 6 && expected_length == 0) {
                            uint16_t data_length = (rx_buffer[4] << 8) | rx_buffer[5];
                            expected_length = UART_PACKET_HEADER_SIZE + data_length;
                            
                            if (expected_length > UART_MAX_PACKET_SIZE || expected_length < UART_PACKET_HEADER_SIZE) {
                                rx_index = 0;
                                in_packet = false;
                                expected_length = 0;
                                continue;
                            }
                        }
                        
                        if (expected_length > 0 && rx_index == expected_length) {
                            audio_packet_t audio_packet;
                            if (audioMgr->parseUARTPacket(rx_buffer, rx_index, &audio_packet)) {
                                // Обработка исходящего аудио
                                audioMgr->processOutgoingAudio(audio_packet.call_id,
                                                              audio_packet.data,
                                                              audio_packet.data_length,
                                                              audio_packet.codec_type,
                                                              audio_packet.timestamp);
                                
                                if (audio_packet.data) {
                                    free(audio_packet.data);
                                }
                            }
                            
                            rx_index = 0;
                            in_packet = false;
                            expected_length = 0;
                        }
                    } else {
                        rx_index = 0;
                        in_packet = false;
                        expected_length = 0;
                    }
                }
            }
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void AudioManager::audioProcessTask(void* pvParameters) {
    AudioManager* audioMgr = (AudioManager*)pvParameters;
    
    Serial.println("AudioManager: Задача обработки аудио запущена");
    
    while (1) {
        // Обработка аудио для всех активных вызовов
        if (audioMgr->config_manager) {
            for (int i = 0; i < audioMgr->config_manager->getMaxCalls(); i++) {
                if (audioMgr->call_states[i].is_active) {
                    // Очистка неактивных вызовов (таймаут 30 секунд)
                    if (millis() - audioMgr->call_states[i].last_activity > 30000) {
                        audioMgr->call_states[i].is_active = false;
                        Serial.printf("AudioManager: Вызов %d деактивирован по таймауту\n", i);
                    }
                }
            }
        }
        
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void AudioManager::setCallActive(int call_id, bool active) {
    if (config_manager && call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        call_states[call_id].is_active = active;
        call_states[call_id].last_activity = millis();
        
        if (active) {
            // Сброс sequence и timestamp при активации
            call_states[call_id].last_sequence = 0;
            call_states[call_id].timestamp_initialized = false;
            global_clock.reset();
        }
        
        sendCallStatusToAudioKit(call_id, active);
        Serial.printf("AudioManager: Call %d %s\n", call_id, active ? "ACTIVATED" : "DEACTIVATED");
    }
}

bool AudioManager::isCallActive(int call_id) const {
    if (config_manager && call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        return call_states[call_id].is_active;
    }
    return false;
}

void AudioManager::configureCall(int call_id, uint8_t codec_type, uint16_t clock_rate) {
    if (!config_manager || call_id < 0 || call_id >= config_manager->getMaxCalls()) {
        return;
    }
    
    // Сохраняем настройки в структуре вызова
    call_states[call_id].active_codec = codec_type;
    call_states[call_id].is_active = true;
    call_states[call_id].last_activity = millis();
    
    // Отправляем настройки на AudioKit
    sendCallSettingsToAudioKit(call_id, codec_type, clock_rate);
    
    Serial.printf("AudioManager: Call %d configured - Codec: %d, Clock: %dHz\n", 
                 call_id, codec_type, clock_rate);
}

void AudioManager::setActiveCodec(int call_id, uint8_t codec_type) {
    if (config_manager && call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        call_states[call_id].active_codec = codec_type;
        call_states[call_id].last_activity = millis();
    }
}

uint8_t AudioManager::getActiveCodec(int call_id) {
    if (config_manager && call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        return call_states[call_id].active_codec;
    }
    return config_manager ? config_manager->getPrimaryCodec() : 8; // PCMA по умолчанию
}

void AudioManager::resetCallAudio(int call_id) {
    if (config_manager && call_id >= 0 && call_id < config_manager->getMaxCalls()) {
        call_states[call_id].is_active = false;
        call_states[call_id].last_activity = 0;
        call_states[call_id].last_sequence = 0;
        call_states[call_id].timestamp_initialized = false;
    }
}

void AudioManager::startTasks() {
    if (tasks_running) return;
    
    xTaskCreate(uartTask, "UART_Task", 4096, this, 12, &uart_task_handle);
    xTaskCreate(audioProcessTask, "Audio_Process", 4096, this, 10, &audio_process_task_handle);
    
    tasks_running = true;
    Serial.println("AudioManager: Задачи запущены");
}

void AudioManager::stopTasks() {
    if (!tasks_running) return;
    
    if (uart_task_handle) {
        vTaskDelete(uart_task_handle);
        uart_task_handle = nullptr;
    }
    if (audio_process_task_handle) {
        vTaskDelete(audio_process_task_handle);
        audio_process_task_handle = nullptr;
    }
    
    tasks_running = false;
    Serial.println("AudioManager: Задачи остановлены");
}

void AudioManager::sendCallStatusToAudioKit(int call_id, bool active) {
    uint8_t command_packet[6];
    
    command_packet[0] = 0x5A;
    command_packet[1] = 0xA5;
    command_packet[2] = 0x01;
    command_packet[3] = call_id;
    command_packet[4] = active ? 0x01 : 0x00;
    command_packet[5] = 0x00;
    
    uart_write_bytes(UART_PORT, (const char*)command_packet, sizeof(command_packet));
    
    Serial.printf("AudioManager: Sent call status to AudioKit - Call%d: %s\n",
                 call_id, active ? "ACTIVE" : "INACTIVE");
}

void AudioManager::sendCallSettingsToAudioKit(int call_id, uint8_t codec_type, uint16_t clock_rate) {
    if (!config_manager || call_id < 0 || call_id >= config_manager->getMaxCalls()) {
        return;
    }
    
    uint8_t settings_packet[10];
    
    settings_packet[0] = 0x5A;
    settings_packet[1] = 0xA5;
    settings_packet[2] = 0x02;
    settings_packet[3] = call_id;
    settings_packet[4] = codec_type;
    settings_packet[5] = (clock_rate >> 8) & 0xFF;
    settings_packet[6] = clock_rate & 0xFF;
    settings_packet[7] = 0x00;
    settings_packet[8] = 0x00;
    settings_packet[9] = 0x00;
    
    uart_write_bytes(UART_PORT, (const char*)settings_packet, sizeof(settings_packet));
    
    Serial.printf("AudioManager: Sent call settings to AudioKit - Call%d: Codec=%d, Clock=%dHz\n",
                 call_id, codec_type, clock_rate);
}

void AudioManager::process() {
    // Основная обработка вынесена в отдельные задачи
}