/* M2 audio engine: ES8311 I2S codec full-duplex.
 * Read from codec ADC (mic) -> DSP chain -> write to codec DAC (speaker).
 * Same DSP as M1: 300 Hz-3 kHz bandpass -> soft-clip -> bitcrush -> static bed,
 * VOX 4-state gate with key-up/key-down click bursts. */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "bsp/esp32_s3_touch_amoled_1_8.h"
#include "audio.h"
#include "settings.h"

#define TAG "audio"

#define SAMPLE_RATE_HZ   16000
#define BLOCK_SAMPLES    256        /* 16 ms blocks */

static esp_codec_dev_handle_t s_spk = NULL;
static esp_codec_dev_handle_t s_mic = NULL;

static volatile audio_mode_t s_mode = AUDIO_MODE_TONE;
static volatile int s_gain_in = 100, s_gain_out = 100;
static volatile int s_level = 0;
static volatile bool s_clip = false;

/* 64-entry sine LUT for tone mode, Q15 */
static int16_t s_sine[64];
static uint32_t s_tone_phase = 0;
#define TONE_PHASE_INC ((uint32_t)(440.0 * 64.0 / SAMPLE_RATE_HZ * 65536.0))

/* ---------- stormtrooper DSP (unchanged from M1) ---------- */

typedef struct { float b0, b1, b2, a1, a2, z1, z2; } biquad_t;
static biquad_t s_hpf, s_lpf;

static void biquad_set(biquad_t *q, float f0, bool highpass)
{
    float w = 2.0f * (float)M_PI * f0 / SAMPLE_RATE_HZ;
    float c = cosf(w), s = sinf(w), alpha = s / (2.0f * 0.707f);
    float a0 = 1 + alpha;
    if (highpass) {
        q->b0 = (1 + c) / 2 / a0; q->b1 = -(1 + c) / a0; q->b2 = q->b0;
    } else {
        q->b0 = (1 - c) / 2 / a0; q->b1 = (1 - c) / a0; q->b2 = q->b0;
    }
    q->a1 = (-2 * c) / a0; q->a2 = (1 - alpha) / a0;
    q->z1 = q->z2 = 0;
}

static inline float biquad_run(biquad_t *q, float x)
{
    float y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2;
    q->z2 = q->b2 * x - q->a2 * y;
    return y;
}

static uint32_t s_rng = 0xC0FFEE;
static inline float noisef(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return ((int32_t)s_rng >> 8) / 8388608.0f;
}

typedef enum { VOX_IDLE, VOX_KEYUP, VOX_OPEN, VOX_KEYDOWN } vox_state_t;
static vox_state_t s_vox = VOX_IDLE;
static float s_env = 0;
static int s_hang = 0;
static int s_click_n = 0;
static float s_click_amp = 0;
static int s_click_variant = 0;
#define VOX_HANG_SAMPLES (SAMPLE_RATE_HZ * 35 / 100)

static void start_click(bool keyup)
{
    static int last = -1;
    int v;
    do { v = (int)(noisef() * 100.0f) & 3; } while (v == last);
    last = v;
    s_click_variant = v;
    s_click_n = SAMPLE_RATE_HZ * (keyup ? 30 : 50 + 10 * v) / 1000;
    s_click_amp = 0.9f * g_settings.click_vol / 100.0f;
}

static inline float fx_process(float x)
{
    float fx = g_settings.fx_intensity / 100.0f;
    x = biquad_run(&s_hpf, x);
    x = biquad_run(&s_lpf, x);
    float drive = 1.0f + 7.0f * fx;
    x *= drive;
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
    x = x - (x * x * x) / 3.0f;
    float steps = 256.0f - 192.0f * fx;
    x = (int32_t)(x * steps) / steps;
    return x;
}

/* ---------- audio task ---------- */

static void audio_task(void *arg)
{
    static int16_t in_buf[BLOCK_SAMPLES];
    static int16_t out_buf[BLOCK_SAMPLES];

    while (1) {
        int got = esp_codec_dev_read(s_mic, in_buf, sizeof(in_buf));
        if (got <= 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        int samples = got / sizeof(int16_t);
        int peak = 0;
        bool any_clip = false;
        audio_mode_t m = s_mode;

        for (int i = 0; i < samples; i++) {
            int32_t s = ((int32_t)in_buf[i] * s_gain_in) / 100;
            if (s > 32767)  { s = 32767;  any_clip = true; }
            if (s < -32768) { s = -32768; any_clip = true; }

            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;

            int16_t out = 0;

            if (m == AUDIO_MODE_TONE) {
                s_tone_phase += TONE_PHASE_INC;
                out = s_sine[(s_tone_phase >> 16) & 63] / 4;
            } else if (m == AUDIO_MODE_PASSTHROUGH) {
                float xf = (float)s;
                float env_a = fabsf(xf / 32768.0f);
                s_env += (env_a - s_env) * (env_a > s_env ? 0.05f : 0.002f);
                bool above = (s_env * 100.0f) > (float)g_settings.vox_thresh;

                switch (s_vox) {
                case VOX_IDLE:
                    if (above) { s_vox = VOX_KEYUP; start_click(true); }
                    break;
                case VOX_KEYUP:
                    if (s_click_n <= 0) s_vox = VOX_OPEN;
                    break;
                case VOX_OPEN:
                    if (above) s_hang = VOX_HANG_SAMPLES;
                    else if (--s_hang <= 0) { s_vox = VOX_KEYDOWN; start_click(false); }
                    break;
                case VOX_KEYDOWN:
                    if (s_click_n <= 0) s_vox = VOX_IDLE;
                    break;
                }

                float outf = 0;
                if (s_vox == VOX_OPEN) {
                    outf = fx_process(xf / 32768.0f);
                    outf += noisef() * 0.12f * (g_settings.static_lvl / 100.0f);
                } else if (s_click_n > 0) {
                    s_click_n--;
                    float env = (float)s_click_n / (SAMPLE_RATE_HZ * 0.05f);
                    if (env > 1.0f) env = 1.0f;
                    outf = biquad_run(&s_lpf, noisef()) * s_click_amp * env
                           * (1.2f + 0.3f * s_click_variant);
                }

                int32_t os = (int32_t)(outf * 32767.0f);
                os = (os * s_gain_out) / 100;
                if (os > 32767)  { os = 32767;  any_clip = true; }
                if (os < -32768) { os = -32768; any_clip = true; }
                out = (int16_t)os;
            }
            /* AUDIO_MODE_MUTE: out stays 0 */
            out_buf[i] = out;
        }

        s_level = (peak * 100) >> 15;
        if (any_clip) s_clip = true;

        esp_codec_dev_write(s_spk, out_buf, samples * sizeof(int16_t));
    }
}

/* ---------- public API ---------- */

esp_err_t audio_init(void)
{
    biquad_set(&s_hpf, 300.0f, true);
    biquad_set(&s_lpf, 3000.0f, false);
    for (int i = 0; i < 64; i++)
        s_sine[i] = (int16_t)(sinf(i * 2.0f * (float)M_PI / 64.0f) * 32767.0f);

    /* Init I2S at 16 kHz mono to match our DSP rate */
    i2s_std_config_t i2s_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(
                        I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(bsp_audio_init(&i2s_cfg), TAG, "bsp_audio_init");

    s_spk = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(s_spk, ESP_FAIL, TAG, "speaker init failed");

    s_mic = bsp_audio_codec_microphone_init();
    ESP_RETURN_ON_FALSE(s_mic, ESP_FAIL, TAG, "mic init failed");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate    = SAMPLE_RATE_HZ,
        .channel        = 1,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_spk, &fs), TAG, "spk open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_mic, &fs), TAG, "mic open");

    /* Speaker volume: 70% (headroom for gain_out slider) */
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(s_spk, 70), TAG, "spk vol");
    /* Mic gain: +30 dB (reasonable for a boom condenser; user trims via MIC slider) */
    ESP_RETURN_ON_ERROR(esp_codec_dev_set_in_gain(s_mic, 30.0f), TAG, "mic gain");

    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio up: 16kHz mono, ES8311 I2S codec");
    return ESP_OK;
}

void audio_set_mode(audio_mode_t m) { s_mode = m; }
audio_mode_t audio_get_mode(void) { return s_mode; }
void audio_set_gain_in(int p)  { s_gain_in = p; }
void audio_set_gain_out(int p) { s_gain_out = p; }
int  audio_get_level(void)     { return s_level; }
bool audio_get_clip(void)      { bool c = s_clip; s_clip = false; return c; }
