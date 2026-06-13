/* M1 audio engine: ADC continuous capture (GPIO4) -> gain -> ring buffer ->
 * sigma-delta output (GPIO5) clocked by a 16 kHz GPTimer ISR. */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_adc/adc_continuous.h"
#include "driver/sdm.h"
#include "driver/gptimer.h"
#include "audio.h"
#include "settings.h"

#define TAG "audio"

#define SAMPLE_RATE_HZ   16000
#define BLOCK_SAMPLES    256                 /* 16 ms blocks */
#define ADC_FRAME_BYTES  (BLOCK_SAMPLES * SOC_ADC_DIGI_RESULT_BYTES)
#define MIC_ADC_CHANNEL  ADC_CHANNEL_3      /* GPIO 4 */
#define OUT_GPIO         5

/* output ring buffer (power of two) */
#define RING_SIZE 1024
static volatile int16_t s_ring[RING_SIZE];
static volatile uint32_t s_wr = 0, s_rd = 0;

static adc_continuous_handle_t s_adc;
static sdm_channel_handle_t s_sdm;
static gptimer_handle_t s_timer;

static volatile audio_mode_t s_mode = AUDIO_MODE_TONE;
static volatile int s_gain_in = 100, s_gain_out = 100;
static volatile int s_level = 0;
static volatile bool s_clip = false;

/* 64-entry sine LUT for tone mode, Q15 */
static int16_t s_sine[64];
static volatile uint32_t s_tone_phase = 0;
/* 440 Hz at 16 kHz with 64-entry LUT, 16.16 fixed point phase increment */
#define TONE_PHASE_INC ((uint32_t)(440.0 * 64.0 / SAMPLE_RATE_HZ * 65536.0))

/* 16 kHz ISR: pop sample (or tone), apply output gain, drive SDM density */
static bool IRAM_ATTR out_timer_cb(gptimer_handle_t t,
                                   const gptimer_alarm_event_data_t *ed, void *arg)
{
    int32_t sample = 0;
    audio_mode_t m = s_mode;
    if (m == AUDIO_MODE_TONE) {
        s_tone_phase += TONE_PHASE_INC;
        sample = s_sine[(s_tone_phase >> 16) & 63] / 4; /* -12 dB tone */
    } else if (m == AUDIO_MODE_PASSTHROUGH && s_rd != s_wr) {
        sample = s_ring[s_rd & (RING_SIZE - 1)];
        s_rd++;
    }
    sample = (sample * s_gain_out) / 100;
    if (sample > 32767) { sample = 32767; }
    if (sample < -32768) { sample = -32768; }
    /* Q15 -> SDM density -128..127 */
    sdm_channel_set_pulse_density(s_sdm, (int8_t)(sample >> 8));
    return false;
}

/* ---------------- stormtrooper DSP ----------------
 * band-pass 300 Hz..3 kHz -> soft-clip drive -> bitcrush -> static bed,
 * gated by VOX with key-up/key-down click bursts. Floats: S3 has FPU,
 * trivial load at 16 kHz. */

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
static inline float noisef(void) /* white noise -1..1 */
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return ((int32_t)s_rng >> 8) / 8388608.0f;
}

/* VOX state */
typedef enum { VOX_IDLE, VOX_KEYUP, VOX_OPEN, VOX_KEYDOWN } vox_state_t;
static vox_state_t s_vox = VOX_IDLE;
static float s_env = 0;
static int s_hang = 0;
static int s_click_n = 0;       /* samples remaining in click burst */
static float s_click_amp = 0;
static int s_click_variant = 0; /* placeholder pool: vary decay/len; real WAVs drop in for M2 final */
#define VOX_HANG_SAMPLES (SAMPLE_RATE_HZ * 35 / 100)  /* 350 ms hang */

static void start_click(bool keyup)
{
    static int last = -1;
    int v;
    do { v = (int)(noisef() * 100.0f) & 3; } while (v == last);
    last = v;
    s_click_variant = v;
    s_click_n = SAMPLE_RATE_HZ * (keyup ? 30 : 50 + 10 * v) / 1000; /* 30-90 ms */
    s_click_amp = 0.9f * g_settings.click_vol / 100.0f;
}

/* per-sample effect; in/out Q15 as float */
static inline float fx_process(float x)
{
    float fx = g_settings.fx_intensity / 100.0f;

    /* band-pass */
    x = biquad_run(&s_hpf, x);
    x = biquad_run(&s_lpf, x);

    /* soft-clip drive: 1x..8x into tanh-ish curve */
    float drive = 1.0f + 7.0f * fx;
    x *= drive;
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
    x = x - (x * x * x) / 3.0f;

    /* subtle bitcrush, deepens with intensity */
    float steps = 256.0f - 192.0f * fx;   /* 8-bit-ish down to ~6-bit */
    x = (int32_t)(x * steps) / steps;

    return x;
}

static void vox_and_push(float x_q15, bool *clipped)
{
    /* envelope */
    float a = fabsf(x_q15);
    s_env += (a - s_env) * (a > s_env ? 0.05f : 0.002f);
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

    float out = 0;
    if (s_vox == VOX_OPEN) {
        out = fx_process(x_q15 / 32768.0f);
        /* static bed while keyed */
        out += noisef() * 0.12f * (g_settings.static_lvl / 100.0f);
    } else if (s_click_n > 0) {
        /* click burst: decaying band-passed noise pop */
        s_click_n--;
        float env = (float)s_click_n / (SAMPLE_RATE_HZ * 0.05f);
        if (env > 1.0f) env = 1.0f;
        out = biquad_run(&s_lpf, noisef()) * s_click_amp * env * (1.2f + 0.3f * s_click_variant);
    }

    int32_t s = (int32_t)(out * 32767.0f);
    if (s > 32767) { s = 32767; *clipped = true; }
    if (s < -32768) { s = -32768; *clipped = true; }
    if ((s_wr - s_rd) < RING_SIZE) {
        s_ring[s_wr & (RING_SIZE - 1)] = (int16_t)s;
        s_wr++;
    }
}

/* capture task: read ADC frames, center, gain, level meter, push to ring */
static void capture_task(void *arg)
{
    static uint8_t buf[ADC_FRAME_BYTES];
    uint32_t got = 0;
    while (1) {
        if (adc_continuous_read(s_adc, buf, sizeof(buf), &got, 100) != ESP_OK) {
            continue;
        }
        int peak = 0;
        for (uint32_t i = 0; i < got; i += SOC_ADC_DIGI_RESULT_BYTES) {
            adc_digi_output_data_t *d = (adc_digi_output_data_t *)&buf[i];
            int32_t s = ((int32_t)d->type2.data - 2048) << 4;   /* 12-bit -> Q15, centered */
            s = (s * s_gain_in) / 100;
            if (s > 32767) { s = 32767; s_clip = true; }
            if (s < -32768) { s = -32768; s_clip = true; }
            int a = s < 0 ? -s : s;
            if (a > peak) peak = a;
            if (s_mode == AUDIO_MODE_PASSTHROUGH) {
                bool c = false;
                vox_and_push((float)s, &c);
                if (c) s_clip = true;
            }
        }
        s_level = (peak * 100) >> 15;
    }
}

esp_err_t audio_init(void)
{
    biquad_set(&s_hpf, 300.0f, true);
    biquad_set(&s_lpf, 3000.0f, false);
    for (int i = 0; i < 64; i++) {
        s_sine[i] = (int16_t)(sinf(i * 2.0f * (float)M_PI / 64.0f) * 32767.0f);
    }

    /* ADC continuous on GPIO4 / ADC1_CH3 */
    adc_continuous_handle_cfg_t hcfg = {
        .max_store_buf_size = ADC_FRAME_BYTES * 4,
        .conv_frame_size = ADC_FRAME_BYTES,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_new_handle(&hcfg, &s_adc), TAG, "adc handle");
    adc_digi_pattern_config_t pat = {
        .atten = ADC_ATTEN_DB_12,
        .channel = MIC_ADC_CHANNEL,
        .unit = ADC_UNIT_1,
        .bit_width = ADC_BITWIDTH_12,
    };
    adc_continuous_config_t ccfg = {
        .pattern_num = 1,
        .adc_pattern = &pat,
        .sample_freq_hz = SAMPLE_RATE_HZ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    ESP_RETURN_ON_ERROR(adc_continuous_config(s_adc, &ccfg), TAG, "adc config");
    ESP_RETURN_ON_ERROR(adc_continuous_start(s_adc), TAG, "adc start");

    /* sigma-delta output on GPIO5 */
    sdm_config_t scfg = {
        .clk_src = SDM_CLK_SRC_DEFAULT,
        .gpio_num = OUT_GPIO,
        .sample_rate_hz = 1000000,   /* 1 MHz pulse density */
    };
    ESP_RETURN_ON_ERROR(sdm_new_channel(&scfg, &s_sdm), TAG, "sdm");
    ESP_RETURN_ON_ERROR(sdm_channel_enable(s_sdm), TAG, "sdm en");

    /* 16 kHz sample clock */
    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&tcfg, &s_timer), TAG, "timer");
    gptimer_alarm_config_t acfg = {
        .alarm_count = 1000000 / SAMPLE_RATE_HZ,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(s_timer, &acfg), TAG, "alarm");
    gptimer_event_callbacks_t cbs = { .on_alarm = out_timer_cb };
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(s_timer, &cbs, NULL), TAG, "cb");
    ESP_RETURN_ON_ERROR(gptimer_enable(s_timer), TAG, "timer en");
    ESP_RETURN_ON_ERROR(gptimer_start(s_timer), TAG, "timer start");

    xTaskCreatePinnedToCore(capture_task, "audio_cap", 4096, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio up: 16kHz, mic GPIO4, out GPIO5");
    return ESP_OK;
}

void audio_set_mode(audio_mode_t m) { s_mode = m; }
audio_mode_t audio_get_mode(void) { return s_mode; }
void audio_set_gain_in(int p) { s_gain_in = p; }
void audio_set_gain_out(int p) { s_gain_out = p; }
int audio_get_level(void) { return s_level; }
bool audio_get_clip(void) { bool c = s_clip; s_clip = false; return c; }
