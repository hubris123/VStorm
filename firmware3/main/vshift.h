#pragma once
/* Voice shift: move any wearer's voice onto a target (TROOPER, later CLONE) with a fixed,
 * learned shift. Two independent controls:
 *   pitch ratio   - how high or low the voice is (1.0 = unchanged)
 *   formant ratio - "TONE": the size of the throat / chest (1.0 = unchanged, <1 = bigger)
 * Real-time PSOLA (pitch-synchronous overlap-add) at 16 kHz; each grain is resampled by the
 * formant ratio, so pitch and tone move separately. Latency ~32 ms while on, 0 when off.
 * Learning listens to the wearer read for ~15 s and measures their typical pitch and
 * formants; the ratios that land them on the target follow from that.
 * Pure C, float, no allocation. */
#include <stdbool.h>

typedef struct {
    const char *name;
    float f0;          /* typical speaking pitch, Hz */
    float f2, f3;      /* typical 2nd / 3rd formant, Hz (vocal-tract size) */
} vs_target_t;

/* measured from reference recordings (tools/voice_analysis/report.md) */
extern const vs_target_t VS_TROOPER;

void vshift_init(int rate_hz);
/* on = false: straight through, no delay */
void vshift_set(bool on, float pitch_ratio, float formant_ratio);
float vshift_process(float x);              /* one sample in (-1..1) -> one sample out */

/* live readouts (for the UI) */
float vshift_live_f0(void);                 /* wearer's pitch right now, Hz (0 = not voiced) */

/* learning: measures while vshift_process runs */
typedef struct {
    float f0;                  /* wearer's median pitch, Hz */
    float f1, f2, f3;          /* median formants, Hz (0 = not enough data) */
    float seconds;             /* voiced speech heard */
    float pitch_ratio, formant_ratio;   /* the shift that lands them on the target */
} vs_learn_t;
void vlearn_start(void);
void vlearn_stop(void);
bool vlearn_running(void);
/* current result so far for target t (valid when enough speech was heard) */
bool vlearn_result(const vs_target_t *t, vs_learn_t *out);

/* ratio <-> UI units */
float vs_ratio_to_semitones(float r);
float vs_semitones_to_ratio(float st);
