#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <FS.h>
#include <driver/i2s.h>
#include <driver/adc.h>
#include <math.h>

// ============================================================
// ESP32 + MAX9814 VOICE RECORDER
// 32 kHz ADC -> decimation 2:1 -> 16 kHz WAV
// Mono / 16-bit / 10 seconds
// Upload: multipart/form-data, field name = "audio"
// ============================================================

// -------------------- WiFi --------------------
const char* WIFI_SSID = "NATAXTARI";
const char* WIFI_PASSWORD = "58920732";

// -------------------- Server -------------------
const char* SERVER_URL = "http://192.168.1.187:4000/audio";

// -------------------- Audio --------------------
#define I2S_PORT            I2S_NUM_0
#define I2S_SAMPLE_RATE     32000
#define WAV_SAMPLE_RATE     16000
#define DECIMATION          2
#define RECORD_SECONDS      10

#define TOTAL_WAV_SAMPLES   (WAV_SAMPLE_RATE * RECORD_SECONDS)
#define WAV_DATA_BYTES      (TOTAL_WAV_SAMPLES * sizeof(int16_t))

#define WAV_FILE             "/recording.wav"

#define ADC_CHANNEL          ADC1_CHANNEL_0   // GPIO36
#define ADC_ATTENUATION      ADC_ATTEN_DB_11

#define I2S_DMA_BUF_COUNT    8
#define I2S_DMA_BUF_LEN      256

// ADC raw -> audio scale
const float ADC_SCALE = 16.0f;

// -------------------- Audio processing --------------------
// Speech-oriented processing.
const float HPF_FREQ = 100.0f;
const float HPF_Q    = 0.707f;

const float PEAK_FREQ  = 2200.0f;
const float PEAK_Q     = 0.8f;
const float PEAK_GAIN  = 2.0f;

const float LPF_FREQ = 5000.0f;
const float LPF_Q    = 0.707f;

// AGC
const float AGC_TARGET = 7500.0f;
const float AGC_MIN    = 1.0f;
const float AGC_MAX    = 2.2f;
const float AGC_SMOOTH = 0.03f;

// Gate
const float NOISE_GATE = 350.0f;
const float RMS_FLOOR  = 80.0f;

// Limiter
const float LIMIT_LEVEL = 30000.0f;
const float LIMIT_RATIO = 0.08f;

// DC tracking
float dcLevel = 1435.0f;

const float DC_ALPHA_SILENCE = 0.002f;
const float DC_ALPHA_SIGNAL  = 0.0001f;

float agcGain = 1.0f;

// ============================================================
// Biquad filter
// ============================================================

class Biquad {
public:
    float b0 = 1.0f;
    float b1 = 0.0f;
    float b2 = 0.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;

    float x1 = 0.0f;
    float x2 = 0.0f;
    float y1 = 0.0f;
    float y2 = 0.0f;

    void reset() {
        x1 = x2 = 0.0f;
        y1 = y2 = 0.0f;
    }

    float process(float x) {
        float y =
            b0 * x +
            b1 * x1 +
            b2 * x2 -
            a1 * y1 -
            a2 * y2;

        x2 = x1;
        x1 = x;

        y2 = y1;
        y1 = y;

        return y;
    }

    void setLowPass(float sampleRate, float freq, float Q) {
        float w0 = 2.0f * PI * freq / sampleRate;
        float cosw = cosf(w0);
        float sinw = sinf(w0);
        float alpha = sinw / (2.0f * Q);

        float bb0 = (1.0f - cosw) * 0.5f;
        float bb1 = 1.0f - cosw;
        float bb2 = (1.0f - cosw) * 0.5f;
        float aa0 = 1.0f + alpha;
        float aa1 = -2.0f * cosw;
        float aa2 = 1.0f - alpha;

        b0 = bb0 / aa0;
        b1 = bb1 / aa0;
        b2 = bb2 / aa0;
        a1 = aa1 / aa0;
        a2 = aa2 / aa0;

        reset();
    }

    void setHighPass(float sampleRate, float freq, float Q) {
        float w0 = 2.0f * PI * freq / sampleRate;
        float cosw = cosf(w0);
        float sinw = sinf(w0);
        float alpha = sinw / (2.0f * Q);

        float bb0 = (1.0f + cosw) * 0.5f;
        float bb1 = -(1.0f + cosw);
        float bb2 = (1.0f + cosw) * 0.5f;
        float aa0 = 1.0f + alpha;
        float aa1 = -2.0f * cosw;
        float aa2 = 1.0f - alpha;

        b0 = bb0 / aa0;
        b1 = bb1 / aa0;
        b2 = bb2 / aa0;
        a1 = aa1 / aa0;
        a2 = aa2 / aa0;

        reset();
    }

    void setPeaking(float sampleRate, float freq, float Q, float gainDB) {
        float A = powf(10.0f, gainDB / 40.0f);

        float w0 = 2.0f * PI * freq / sampleRate;
        float cosw = cosf(w0);
        float sinw = sinf(w0);
        float alpha = sinw / (2.0f * Q);

        float bb0 = 1.0f + alpha * A;
        float bb1 = -2.0f * cosw;
        float bb2 = 1.0f - alpha * A;

        float aa0 = 1.0f + alpha / A;
        float aa1 = -2.0f * cosw;
        float aa2 = 1.0f - alpha / A;

        b0 = bb0 / aa0;
        b1 = bb1 / aa0;
        b2 = bb2 / aa0;
        a1 = aa1 / aa0;
        a2 = aa2 / aa0;

        reset();
    }
};

Biquad hpf;
Biquad peaking;
Biquad lpf;

// ============================================================
// WAV header
// ============================================================

void writeWavHeader(
    File& file,
    uint32_t sampleRate,
    uint16_t channels,
    uint16_t bitsPerSample,
    uint32_t dataSize
) {
    uint32_t byteRate =
        sampleRate * channels * bitsPerSample / 8;

    uint16_t blockAlign =
        channels * bitsPerSample / 8;

    uint32_t chunkSize = 36 + dataSize;

    file.seek(0);

    file.write((const uint8_t*)"RIFF", 4);
    file.write((const uint8_t*)&chunkSize, 4);
    file.write((const uint8_t*)"WAVE", 4);

    file.write((const uint8_t*)"fmt ", 4);

    uint32_t subchunk1Size = 16;
    uint16_t audioFormat = 1;

    file.write((const uint8_t*)&subchunk1Size, 4);
    file.write((const uint8_t*)&audioFormat, 2);
    file.write((const uint8_t*)&channels, 2);
    file.write((const uint8_t*)&sampleRate, 4);
    file.write((const uint8_t*)&byteRate, 4);
    file.write((const uint8_t*)&blockAlign, 2);
    file.write((const uint8_t*)&bitsPerSample, 2);

    file.write((const uint8_t*)"data", 4);
    file.write((const uint8_t*)&dataSize, 4);
}

// ============================================================
// ADC extraction
// ESP32 I2S ADC data: ADC value is in bits 16..27
// ============================================================

uint16_t extractADC(uint32_t raw) {
    return (raw >> 16) & 0x0FFF;
}

// ============================================================
// I2S setup
// ============================================================

bool setupI2S() {

    i2s_config_t config;

    memset(&config, 0, sizeof(config));

    config.mode =
        (i2s_mode_t)(
            I2S_MODE_MASTER |
            I2S_MODE_RX |
            I2S_MODE_ADC_BUILT_IN
        );

    config.sample_rate = I2S_SAMPLE_RATE;

    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;

    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;

    config.communication_format = I2S_COMM_FORMAT_I2S_MSB;

    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;

    config.dma_buf_count = I2S_DMA_BUF_COUNT;
    config.dma_buf_len = I2S_DMA_BUF_LEN;

    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(
        I2S_PORT,
        &config,
        0,
        nullptr
    );

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR: i2s_driver_install failed: %d\n",
            err
        );
        return false;
    }

    err = adc1_config_width(ADC_WIDTH_BIT_12);

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR: adc1_config_width failed: %d\n",
            err
        );
        return false;
    }

    err = adc1_config_channel_atten(
        ADC_CHANNEL,
        ADC_ATTENUATION
    );

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR: adc1_config_channel_atten failed: %d\n",
            err
        );
        return false;
    }

    err = i2s_set_adc_mode(
        ADC_UNIT_1,
        ADC_CHANNEL
    );

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR: i2s_set_adc_mode failed: %d\n",
            err
        );
        return false;
    }

    err = i2s_adc_enable(I2S_PORT);

    if (err != ESP_OK) {
        Serial.printf(
            "ERROR: i2s_adc_enable failed: %d\n",
            err
        );
        return false;
    }

    // Throw away old DMA data.
    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("I2S ADC initialized.");

    return true;
}

// ============================================================
// Read one ADC sample
// ============================================================

bool readADCSample(uint16_t& adcValue) {

    uint32_t raw = 0;
    size_t bytesRead = 0;

    esp_err_t err = i2s_read(
        I2S_PORT,
        &raw,
        sizeof(raw),
        &bytesRead,
        portMAX_DELAY
    );

    if (err != ESP_OK || bytesRead != sizeof(raw)) {
        return false;
    }

    adcValue = extractADC(raw);

    return true;
}

// ============================================================
// ADC diagnostic
// ============================================================

void adcDiagnostic() {

    Serial.println();
    Serial.println("==============================");
    Serial.println("ADC diagnostic");
    Serial.println("==============================");

    uint32_t rawBuffer[8];

    for (int block = 0; block < 5; block++) {

        size_t bytesRead = 0;

        esp_err_t err = i2s_read(
            I2S_PORT,
            rawBuffer,
            sizeof(rawBuffer),
            &bytesRead,
            pdMS_TO_TICKS(1000)
        );

        if (err != ESP_OK) {
            Serial.printf(
                "ADC diagnostic read error: %d\n",
                err
            );
            continue;
        }

        Serial.printf(
            "Block %d | bytes: %u\n",
            block,
            (unsigned)bytesRead
        );

        int count = bytesRead / sizeof(uint32_t);

        for (int i = 0; i < count; i++) {

            uint16_t adc = extractADC(rawBuffer[i]);
            uint16_t low12 = rawBuffer[i] & 0x0FFF;

            Serial.printf(
                "raw32: %08lX  ADC: %u  low12: %u\n",
                (unsigned long)rawBuffer[i],
                adc,
                low12
            );
        }
    }

    Serial.println("==============================");
}

// ============================================================
// DC calibration
// ============================================================

bool calibrateDC() {

    Serial.println();
    Serial.println("Calibrating DC...");

    const int samplesToRead = 2000;

    uint64_t sum = 0;
    int validSamples = 0;

    for (int i = 0; i < samplesToRead; i++) {

        uint16_t adc;

        if (!readADCSample(adc)) {
            continue;
        }

        sum += adc;
        validSamples++;
    }

    if (validSamples == 0) {
        Serial.println("ERROR: DC calibration failed");
        return false;
    }

    dcLevel =
        (float)sum / (float)validSamples;

    Serial.printf(
        "DC calibration: %.2f\n",
        dcLevel
    );

    return true;
}

// ============================================================
// Initialize filters
// ============================================================

void setupFilters() {

    hpf.setHighPass(
        WAV_SAMPLE_RATE,
        HPF_FREQ,
        HPF_Q
    );

    peaking.setPeaking(
        WAV_SAMPLE_RATE,
        PEAK_FREQ,
        PEAK_Q,
        PEAK_GAIN
    );

    lpf.setLowPass(
        WAV_SAMPLE_RATE,
        LPF_FREQ,
        LPF_Q
    );

    Serial.println("Audio filters initialized.");
}

// ============================================================
// Process one sample
// ============================================================

int16_t processSample(uint16_t adc) {

    // Slowly track microphone DC.
    float raw = (float)adc;

    float deviation = raw - dcLevel;

    float dcAlpha =
        (fabsf(deviation) < 250.0f)
        ? DC_ALPHA_SILENCE
        : DC_ALPHA_SIGNAL;

    dcLevel +=
        dcAlpha * (raw - dcLevel);

    float x =
        (raw - dcLevel) * ADC_SCALE;

    // High-pass
    x = hpf.process(x);

    // Speech presence boost
    x = peaking.process(x);

    // Low-pass
    x = lpf.process(x);

    return (int16_t)constrain(
        (int32_t)x,
        -32768,
        32767
    );
}

// ============================================================
// Record WAV
// ============================================================

bool recordWav() {

    Serial.println();
    Serial.println("==============================");
    Serial.println("START RECORDING");
    Serial.println("==============================");

    if (SPIFFS.exists(WAV_FILE)) {
        SPIFFS.remove(WAV_FILE);
    }

    File file = SPIFFS.open(
        WAV_FILE,
        FILE_WRITE
    );

    if (!file) {
        Serial.println("ERROR: Cannot create WAV file");
        return false;
    }

    // Reserve header.
    uint8_t emptyHeader[44];
    memset(emptyHeader, 0, sizeof(emptyHeader));

    if (file.write(emptyHeader, sizeof(emptyHeader))
        != sizeof(emptyHeader)) {

        Serial.println("ERROR: Cannot write WAV header");

        file.close();
        return false;
    }

    // Reset processing state.
    hpf.reset();
    peaking.reset();
    lpf.reset();

    agcGain = 1.0f;

    uint32_t samplesWritten = 0;

    uint32_t rawSamplesRead = 0;

    const int BLOCK_SAMPLES = 512;

    int16_t outputBuffer[BLOCK_SAMPLES];

    uint32_t lastProgress = 0;

    uint64_t rmsSum = 0;
    uint32_t rmsCount = 0;

    // We read 32 kHz and keep every second sample.
    bool keepSample = false;

    while (samplesWritten < TOTAL_WAV_SAMPLES) {

        uint16_t adc;

        if (!readADCSample(adc)) {
            Serial.println("ERROR: ADC read failed");
            file.close();
            return false;
        }

        rawSamplesRead++;

        keepSample = !keepSample;

        if (!keepSample) {
            continue;
        }

        int16_t sample = processSample(adc);

        float absSample =
            fabsf((float)sample);

        rmsSum +=
            (uint64_t)(absSample * absSample);

        rmsCount++;

        outputBuffer[
            samplesWritten % BLOCK_SAMPLES
        ] = sample;

        samplesWritten++;

        // Process complete block.
        if (
            samplesWritten % BLOCK_SAMPLES == 0 ||
            samplesWritten == TOTAL_WAV_SAMPLES
        ) {

            uint32_t blockStart =
                samplesWritten -
                (samplesWritten % BLOCK_SAMPLES);

            uint32_t blockSamples =
                samplesWritten % BLOCK_SAMPLES;

            if (blockSamples == 0) {
                blockSamples = BLOCK_SAMPLES;
            }

            // Calculate RMS of the current block.
            float blockRms = 0.0f;

            uint64_t blockEnergy = 0;

            for (uint32_t i = 0; i < blockSamples; i++) {

                float v =
                    (float)outputBuffer[i];

                blockEnergy +=
                    (uint64_t)(v * v);
            }

            if (blockSamples > 0) {
                blockRms =
                    sqrtf(
                        (float)blockEnergy /
                        (float)blockSamples
                    );
            }

            // AGC target.
            float targetGain = AGC_TARGET /
                               max(blockRms, RMS_FLOOR);

            targetGain = constrain(
                targetGain,
                AGC_MIN,
                AGC_MAX
            );

            agcGain +=
                AGC_SMOOTH *
                (targetGain - agcGain);

            // Apply AGC, gate and limiter.
            for (uint32_t i = 0; i < blockSamples; i++) {

                float v =
                    (float)outputBuffer[i] *
                    agcGain;

                // Noise gate.
                if (fabsf(v) < NOISE_GATE) {
                    v = 0.0f;
                }

                // Soft limiter.
                float av = fabsf(v);

                if (av > LIMIT_LEVEL) {

                    float sign =
                        (v >= 0.0f) ? 1.0f : -1.0f;

                    float excess =
                        av - LIMIT_LEVEL;

                    v =
                        sign *
                        (
                            LIMIT_LEVEL +
                            excess * LIMIT_RATIO
                        );
                }

                v = constrain(
                    v,
                    -32768.0f,
                    32767.0f
                );

                outputBuffer[i] =
                    (int16_t)lroundf(v);
            }

            size_t bytesToWrite =
                blockSamples *
                sizeof(int16_t);

            size_t written =
                file.write(
                    (const uint8_t*)outputBuffer,
                    bytesToWrite
                );

            if (written != bytesToWrite) {

                Serial.printf(
                    "ERROR: WAV write failed: %u/%u\n",
                    (unsigned)written,
                    (unsigned)bytesToWrite
                );

                file.close();
                return false;
            }

            uint32_t progress =
                (samplesWritten * 100UL) /
                TOTAL_WAV_SAMPLES;

            if (
                progress >= lastProgress + 5 ||
                samplesWritten == TOTAL_WAV_SAMPLES
            ) {

                Serial.printf(
                    "Recording: %lu%% | RMS: %.0f | DC: %.1f | AGC: %.2f\n",
                    (unsigned long)progress,
                    blockRms,
                    dcLevel,
                    agcGain
                );

                lastProgress = progress;
            }
        }
    }

    // Rewrite WAV header.
    writeWavHeader(
        file,
        WAV_SAMPLE_RATE,
        1,
        16,
        WAV_DATA_BYTES
    );

    file.flush();
    file.close();

    Serial.println();
    Serial.println("Recording finished.");
    Serial.printf(
        "Samples: %lu\n",
        (unsigned long)samplesWritten
    );

    Serial.printf(
        "Audio bytes: %u\n",
        (unsigned)WAV_DATA_BYTES
    );

    File checkFile = SPIFFS.open(
        WAV_FILE,
        FILE_READ
    );

    if (!checkFile) {
        Serial.println("ERROR: Cannot reopen WAV");
        return false;
    }

    size_t finalSize =
        checkFile.size();

    checkFile.close();

    Serial.printf(
        "WAV file size: %u\n",
        (unsigned)finalSize
    );

    if (finalSize != WAV_DATA_BYTES + 44) {

        Serial.printf(
            "ERROR: Unexpected WAV size. Expected %u\n",
            (unsigned)(WAV_DATA_BYTES + 44)
        );

        return false;
    }

    return true;
}

// ============================================================
// Multipart stream
//
// IMPORTANT:
// We DO NOT create /multipart_upload.bin.
// WAV is streamed directly from SPIFFS to HTTPClient.
// ============================================================

class MultipartAudioStream : public Stream {

private:

    File wavFile;

    const char* header;
    const char* footer;

    size_t headerPos = 0;
    size_t footerPos = 0;

    // IMPORTANT:
    // Stream::read() is virtual in this ESP32 Arduino core, while
    // read(uint8_t*, size_t) is NOT virtual. Therefore we implement
    // the required one-byte read(), but internally buffer the WAV file.
    // This avoids doing one SPIFFS File::read() for every HTTP byte.
    static const size_t FILE_BUFFER_SIZE = 1024;
    uint8_t fileBuffer[FILE_BUFFER_SIZE];
    size_t fileBufferPos = 0;
    size_t fileBufferLen = 0;

    enum State {
        HEADER,
        WAV,
        FOOTER,
        DONE
    };

    State state = HEADER;

public:

    MultipartAudioStream(
        File file,
        const char* headerData,
        const char* footerData
    )
        : wavFile(file),
          header(headerData),
          footer(footerData) {
    }

    ~MultipartAudioStream() {
        if (wavFile) {
            wavFile.close();
        }
    }

    int available() override {

        if (state == HEADER) {
            return (int)(strlen(header) - headerPos);
        }

        if (state == WAV) {
            if (!wavFile) {
                return 0;
            }

            size_t buffered = 0;
            if (fileBufferLen > fileBufferPos) {
                buffered = fileBufferLen - fileBufferPos;
            }

            return (int)(buffered + wavFile.available());
        }

        if (state == FOOTER) {
            return (int)(strlen(footer) - footerPos);
        }

        return 0;
    }

    int read() override {

        // Multipart header
        if (state == HEADER) {

            const size_t len = strlen(header);

            if (headerPos < len) {
                return (uint8_t)header[headerPos++];
            }

            state = WAV;
        }

        // WAV file, read from a RAM buffer instead of SPIFFS byte-by-byte.
        if (state == WAV) {

            if (!wavFile) {
                state = FOOTER;
            } else {

                if (fileBufferPos >= fileBufferLen) {
                    fileBufferLen = wavFile.read(fileBuffer, FILE_BUFFER_SIZE);
                    fileBufferPos = 0;

                    if (fileBufferLen == 0) {
                        wavFile.close();
                        state = FOOTER;
                    }
                }

                if (state == WAV && fileBufferPos < fileBufferLen) {
                    return fileBuffer[fileBufferPos++];
                }
            }
        }

        // Multipart footer
        if (state == FOOTER) {

            const size_t len = strlen(footer);

            if (footerPos < len) {
                return (uint8_t)footer[footerPos++];
            }

            state = DONE;
            return -1;
        }

        return -1;
    }

    int peek() override {
        return -1;
    }

    void flush() override {
    }

    size_t write(uint8_t) override {
        return 0;
    }
};

// ============================================================
// Upload WAV
// ============================================================

bool uploadWav() {

    Serial.println();
    Serial.println("==============================");
    Serial.println("Uploading WAV");
    Serial.println("==============================");

    if (!SPIFFS.exists(WAV_FILE)) {
        Serial.println("ERROR: WAV file does not exist");
        return false;
    }

    File wavFile = SPIFFS.open(WAV_FILE, FILE_READ);

    if (!wavFile) {
        Serial.println("ERROR: Cannot open WAV file");
        return false;
    }

    const size_t wavSize = wavFile.size();

    Serial.printf("WAV size: %u\n", (unsigned)wavSize);

    if (wavSize < 44) {
        Serial.println("ERROR: WAV file is too small");
        wavFile.close();
        return false;
    }

    // NestJS expects multipart field name: "audio".
    const char* boundary = "----ESP32AudioBoundary7F91";

    String multipartHeader =
        String("--") + boundary +
        "\r\n"
        "Content-Disposition: form-data; name=\"audio\"; filename=\"recording.wav\"\r\n"
        "Content-Type: audio/wav\r\n"
        "\r\n";

    String multipartFooter =
        String("\r\n--") + boundary + "--\r\n";

    const size_t contentLength =
        multipartHeader.length() + wavSize + multipartFooter.length();

    Serial.printf(
        "Multipart content length: %u\n",
        (unsigned)contentLength
    );

    // Parse the configured URL. This project uses plain HTTP.
    String url = SERVER_URL;
    const String httpPrefix = "http://";

    if (!url.startsWith(httpPrefix)) {
        Serial.println("ERROR: SERVER_URL must start with http://");
        wavFile.close();
        return false;
    }

    String address = url.substring(httpPrefix.length());
    int slashPos = address.indexOf('/');

    String host;
    String path;

    if (slashPos >= 0) {
        host = address.substring(0, slashPos);
        path = address.substring(slashPos);
    } else {
        host = address;
        path = "/";
    }

    uint16_t port = 80;
    int colonPos = host.lastIndexOf(':');

    if (colonPos > 0) {
        port = (uint16_t)host.substring(colonPos + 1).toInt();
        host = host.substring(0, colonPos);
    }

    Serial.printf("Connecting to %s:%u%s\n", host.c_str(), port, path.c_str());

    WiFiClient client;
    client.setTimeout(30000);

    if (!client.connect(host.c_str(), port)) {
        Serial.println("ERROR: TCP connection failed");
        wavFile.close();
        return false;
    }

    Serial.println("TCP connected.");
    Serial.println("Sending multipart request...");

    // HTTP request headers.
    client.print("POST ");
    client.print(path);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(host);
    client.println("User-Agent: ESP32-MAX9814");
    client.println("Connection: close");
    client.print("Content-Type: multipart/form-data; boundary=");
    client.println(boundary);
    client.print("Content-Length: ");
    client.println((unsigned long)contentLength);
    client.println();

    // Multipart header.
    if (client.print(multipartHeader) != multipartHeader.length()) {
        Serial.println("ERROR: Multipart header write failed");
        client.stop();
        wavFile.close();
        return false;
    }

    // Send WAV in reasonably small blocks. This avoids the old
    // Stream/HTTPClient path which was aborting the socket mid-upload.
    static const size_t UPLOAD_BUFFER_SIZE = 1024;
    uint8_t uploadBuffer[UPLOAD_BUFFER_SIZE];

    size_t sentWav = 0;
    uint32_t lastProgress = millis();

    while (sentWav < wavSize) {

        size_t remaining = wavSize - sentWav;
        size_t toRead = remaining > UPLOAD_BUFFER_SIZE
                            ? UPLOAD_BUFFER_SIZE
                            : remaining;

        size_t n = wavFile.read(uploadBuffer, toRead);

        if (n == 0) {
            Serial.println("ERROR: WAV read failed during upload");
            client.stop();
            wavFile.close();
            return false;
        }

        size_t offset = 0;

        while (offset < n) {
            size_t written = client.write(uploadBuffer + offset, n - offset);

            if (written == 0) {
                Serial.println("ERROR: TCP write failed during WAV upload");
                client.stop();
                wavFile.close();
                return false;
            }

            offset += written;
            sentWav += written;

            yield();
        }

        if (millis() - lastProgress >= 500) {
            uint8_t percent = (uint8_t)((sentWav * 100ULL) / wavSize);
            Serial.printf(
                "Upload: %u%% (%u/%u bytes)\n",
                percent,
                (unsigned)sentWav,
                (unsigned)wavSize
            );
            lastProgress = millis();
        }
    }

    wavFile.close();

    // Multipart closing boundary.
    if (client.print(multipartFooter) != multipartFooter.length()) {
        Serial.println("ERROR: Multipart footer write failed");
        client.stop();
        return false;
    }

    Serial.println("WAV body sent.");
    Serial.println("Waiting for server response...");

    // Read HTTP status line.
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();

    Serial.print("HTTP status: ");
    Serial.println(statusLine);

    int httpCode = -1;

    if (statusLine.startsWith("HTTP/1.")) {
        int firstSpace = statusLine.indexOf(' ');
        if (firstSpace >= 0) {
            httpCode = statusLine.substring(firstSpace + 1).toInt();
        }
    }

    // Consume response headers.
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        line.trim();

        if (line.length() == 0) {
            break;
        }
    }

    // Read response body.
    String response;
    uint32_t responseStart = millis();

    while (client.connected() || client.available()) {
        while (client.available()) {
            char c = (char)client.read();
            response += c;
        }

        if (millis() - responseStart > 30000) {
            break;
        }

        delay(1);
    }

    client.stop();

    Serial.println("Server response:");
    Serial.println(response);

    if (httpCode >= 200 && httpCode < 300) {
        Serial.println("UPLOAD SUCCESS");
        return true;
    }

    Serial.printf("UPLOAD FAILED, HTTP code: %d\n", httpCode);
    return false;
}

// ============================================================
// WiFi
// ============================================================

bool connectWiFi() {

    Serial.println();
    Serial.println(
        "Connecting to WiFi..."
    );

    WiFi.mode(WIFI_STA);

    // Prevent WiFi sleep during upload.
    WiFi.setSleep(false);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    uint32_t start =
        millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - start < 20000
    ) {

        delay(500);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {

        Serial.println(
            "ERROR: WiFi connection failed"
        );

        return false;
    }

    Serial.println(
        "WiFi connected."
    );

    Serial.print(
        "IP address: "
    );

    Serial.println(
        WiFi.localIP()
    );

    Serial.printf(
        "RSSI: %d\n",
        WiFi.RSSI()
    );

    return true;
}

// ============================================================
// Setup
// ============================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println(
        "================================"
    );
    Serial.println(
        "ESP32 MAX9814 Voice Recorder"
    );
    Serial.println(
        "10 seconds / 16 kHz / 16 bit"
    );
    Serial.println(
        "================================"
    );

    // ------------------------
    // SPIFFS
    // ------------------------

    if (!SPIFFS.begin(true)) {

        Serial.println(
            "ERROR: SPIFFS initialization failed"
        );

        while (true) {
            delay(1000);
        }
    }

    Serial.println(
        "SPIFFS initialized."
    );

    // ------------------------
    // Filters
    // ------------------------

    setupFilters();

    // ------------------------
    // I2S ADC
    // ------------------------

    if (!setupI2S()) {

        Serial.println(
            "ERROR: I2S initialization failed"
        );

        while (true) {
            delay(1000);
        }
    }

    // ------------------------
    // ADC diagnostic
    // ------------------------

    adcDiagnostic();

    // ------------------------
    // DC calibration
    // ------------------------

    if (!calibrateDC()) {

        Serial.println(
            "ERROR: DC calibration failed"
        );

        while (true) {
            delay(1000);
        }
    }

    // ------------------------
    // WiFi
    // ------------------------

    if (!connectWiFi()) {

        while (true) {
            delay(1000);
        }
    }

    Serial.println();
    Serial.println(
        "Server:"
    );
    Serial.println(
        SERVER_URL
    );

    Serial.println();
    Serial.println(
        "READY"
    );
}

// ============================================================
// Loop
// ============================================================

void loop() {

    Serial.println();
    Serial.println(
        "Starting recording..."
    );

    bool recorded =
        recordWav();

    if (!recorded) {

        Serial.println();
        Serial.println(
            "Recording failed!"
        );

        delay(3000);

        return;
    }

    bool uploaded =
        uploadWav();

    if (!uploaded) {

        Serial.println();
        Serial.println(
            "Upload failed!"
        );
    }

    Serial.println();
    Serial.println(
        "DONE."
    );

    // One recording per loop.
    // Change this delay if continuous recording is needed.
    delay(3000);
}
