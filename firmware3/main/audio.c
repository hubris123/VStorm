/* Stormtrooper audio engine - firmware3.
 * ES8311 codec + raw I2S (from the proven 06_I2SCodec example) with our DSP
 * inserted into the full-duplex loop:
 *   300Hz-3kHz bandpass -> soft-clip drive -> bitcrush -> static bed,
 *   VOX 4-state gate with key-up/key-down click bursts.
 * ES8311 is a mono codec wired in I2S stereo mode: mic data arrives on the
 * left slot; we process mono and write the result to both output slots. */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "es8311.h"
#include "esp_log.h"
#include "esp_check.h"
#include "audio.h"

#define TAG "audio"

#define SAMPLE_RATE_HZ   16000
#define MCLK_MULTIPLE    384
#define MCLK_FREQ_HZ     (SAMPLE_RATE_HZ * MCLK_MULTIPLE)
#define FRAMES           256                 /* stereo frames per block */

/* I2S pins (06_I2SCodec) */
#define PIN_MCK  GPIO_NUM_16
#define PIN_BCK  GPIO_NUM_9
#define PIN_WS   GPIO_NUM_45
#define PIN_DO   GPIO_NUM_8
#define PIN_DI   GPIO_NUM_10
#define PIN_PA   GPIO_NUM_46

/* DSP parameters (settings/NVS comes in a later step) */
static int s_fx_intensity = 70;
static int s_vox_thresh   = 22;   /* higher: only open on real speech, not noise floor */
static int s_static_lvl   = 15;
static int s_click_vol    = 60;

static i2s_chan_handle_t s_tx, s_rx;
static volatile int  s_level = 0;
static volatile bool s_clip  = false;
static volatile bool s_muted = true;   /* default muted - safe for bench testing */

/* ---------- stormtrooper DSP (unchanged from the 1.47 firmware) ---------- */
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
    s_click_amp = 0.9f * s_click_vol / 100.0f;
}

static inline float fx_process(float x)
{
    float fx = s_fx_intensity / 100.0f;
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
    static int16_t in_buf[FRAMES * 2];   /* stereo interleaved */
    static int16_t out_buf[FRAMES * 2];
    size_t got = 0, wrote = 0;

    while (1) {
        if (i2s_channel_read(s_rx, in_buf, sizeof(in_buf), &got, portMAX_DELAY) != ESP_OK)
            continue;
        int frames = got / (2 * sizeof(int16_t));
        int peak = 0;
        bool any_clip = false;

        for (int i = 0; i < frames; i++) {
            int32_t s = in_buf[i * 2];   /* left slot = ES8311 mic */
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;

            float xf = (float)s;
            float env_a = fabsf(xf / 32768.0f);
            s_env += (env_a - s_env) * (env_a > s_env ? 0.05f : 0.002f);
            bool above = (s_env * 100.0f) > (float)s_vox_thresh;

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
                outf += noisef() * 0.12f * (s_static_lvl / 100.0f);
            } else if (s_click_n > 0) {
                s_click_n--;
                float env = (float)s_click_n / (SAMPLE_RATE_HZ * 0.05f);
                if (env > 1.0f) env = 1.0f;
                outf = biquad_run(&s_lpf, noisef()) * s_click_amp * env
                       * (1.2f + 0.3f * s_click_variant);
            }

            int32_t os = (int32_t)(outf * 32767.0f);
            if (os > 32767)  { os = 32767;  any_clip = true; }
            if (os < -32768) { os = -32768; any_clip = true; }
            out_buf[i * 2]     = (int16_t)os;   /* write both slots */
            out_buf[i * 2 + 1] = (int16_t)os;
        }

        s_level = (peak * 100) >> 15;
        if (any_clip) s_clip = true;

        if (s_muted) memset(out_buf, 0, frames * 2 * sizeof(int16_t));
        i2s_channel_write(s_tx, out_buf, frames * 2 * sizeof(int16_t), &wrote, portMAX_DELAY);
    }
}

/* ---------- init ---------- */
static esp_err_t es8311_codec_init(i2c_master_bus_handle_t bus)
{
    es8311_handle_t h = es8311_create(bus, ES8311_ADDRRES_0);
    ESP_RETURN_ON_FALSE(h, ESP_FAIL, TAG, "es8311 create failed");
    const es8311_clock_config_t clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = MCLK_FREQ_HZ,
        .sample_frequency = SAMPLE_RATE_HZ,
    };
    ESP_RETURN_ON_ERROR(es8311_init(h, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16),
                        TAG, "es8311 init");
    ESP_RETURN_ON_ERROR(es8311_sample_frequency_config(h, MCLK_FREQ_HZ, SAMPLE_RATE_HZ),
                        TAG, "es8311 fs");
    ESP_RETURN_ON_ERROR(es8311_voice_volume_set(h, 55, NULL), TAG, "es8311 vol");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(h, false), TAG, "es8311 mic");
    ESP_RETURN_ON_ERROR(es8311_microphone_gain_set(h, ES8311_MIC_GAIN_18DB), TAG, "es8311 gain");
    return ESP_OK;
}

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s chan");
    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_MCK, .bclk = PIN_BCK, .ws = PIN_WS,
            .dout = PIN_DO,  .din = PIN_DI,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    cfg.clk_cfg.mclk_multiple = MCLK_MULTIPLE;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &cfg), TAG, "i2s tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &cfg), TAG, "i2s rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx en");
    return ESP_OK;
}

esp_err_t audio_init(i2c_master_bus_handle_t i2c_bus)
{
    biquad_set(&s_hpf, 300.0f, true);
    biquad_set(&s_lpf, 3000.0f, false);

    /* enable the class-D amplifier */
    gpio_config_t pa = { .mode = GPIO_MODE_OUTPUT, .pin_bit_mask = 1ULL << PIN_PA };
    gpio_config(&pa);
    gpio_set_level(PIN_PA, 1);

    ESP_RETURN_ON_ERROR(es8311_codec_init(i2c_bus), TAG, "codec");
    ESP_RETURN_ON_ERROR(i2s_init(), TAG, "i2s");

    xTaskCreatePinnedToCore(audio_task, "audio", 8192, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio up: 16kHz, ES8311 I2S full-duplex");
    return ESP_OK;
}

int  audio_get_level(void) { return s_level; }
bool audio_get_clip(void)  { bool c = s_clip; s_clip = false; return c; }
void audio_set_mute(bool m) { s_muted = m; }
bool audio_is_muted(void)   { return s_muted; }
