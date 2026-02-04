#include "audio_engine.h"
#include "dsl.h"

#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudioTypes.h>
#include <CoreAudio/AudioHardware.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define MAX_VOICES 32
#define COMB_MAX_SAMPLES 4096

typedef enum {
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE,
    ENV_OFF
} EnvStage;

typedef struct {
    bool active;
    SynthType type;
    float freq;
    float target_freq;
    float glide_step;
    int glide_samples;
    float phase;
    float env;
    int age;
    float pitch_env;
    float pitch_decay;
    float hp_state;
    float svf_lp;
    float svf_bp;
    float atk_inc;
    float dec_inc;
    float rel_inc;
    float sus;
    EnvStage stage;
    int gate_samples;
    float cutoff;
    float filter_state;
    uint32_t rng;
    float amp;
    float res;
    float accent;
    float accent_prob;
    float comb_buf[COMB_MAX_SAMPLES];
    int comb_idx;
    int comb_len;
    float comb_feedback;
    float comb_damp;
    float comb_state;
    float crush_hold;
    int crush_count;
} Voice;

typedef struct {
    const PatternDef *pattern;
    const SynthDef *synth;
    const SequenceDef *sequence;
    int step_index;
    int samples_until_step;
    int samples_per_step;
    int every;
    int rev;
    int palindrome;
    int iter;
    int chunk;
    int stut;
    float density;
    uint32_t rng;
    int stut_remaining;
    int stut_samples_until;
    int stut_samples_per;
    float stut_freq;
    int seq_index;
    int seq_repeat_left;
    int seq_repeat_done;
    float slide_ms;
    float ornament_prob;
    int ornament_mode;
    int ornament_alt;
    float accent_prob;
    int seq_start;
    int seq_end;
    int seq_pos;
    int seq_cycle_active;
    float base_rate;
    int is_tempo_leader;
} TrackRuntime;

typedef struct {
    AudioUnit audio_unit;
    double sample_rate;
    int buffer_frames;
    unsigned int output_device_id;
    int bit_depth;

    Program program;
    TrackRuntime tracks[DSL_MAX_TRACKS];
    int track_count;

    Voice voices[MAX_VOICES];

    int base_samples_per_step;

    volatile float meter_l;
    volatile float meter_r;
    volatile float meter_peak_l;
    volatile float meter_peak_r;
    volatile int meter_clip;

    bool running;
    unsigned long long pattern_epoch;
    int tempo_section;
} EngineState;

static EngineState g_engine;

static int is_pm_type(SynthType t) {
    return (t == SYNTH_PM_STRING || t == SYNTH_PM_BELL || t == SYNTH_PM_PIPE ||
            t == SYNTH_PM_KICK || t == SYNTH_PM_SNARE || t == SYNTH_PM_HAT ||
            t == SYNTH_PM_CLAP || t == SYNTH_PM_TOM);
}

static int is_pm_drum(SynthType t) {
    return (t == SYNTH_PM_KICK || t == SYNTH_PM_SNARE || t == SYNTH_PM_HAT ||
            t == SYNTH_PM_CLAP || t == SYNTH_PM_TOM);
}

static int effective_pattern_length(const EngineState *engine, const PatternDef *pattern) {
    if (!engine || !pattern) {
        return 0;
    }
    int len = pattern->length;
    if (len <= 0) {
        return 0;
    }
    if (!engine->program.time_sig_enforce) {
        return len;
    }
    int section = engine->tempo_section;
    if (section < 1 || section > 14) {
        section = 1;
    }
    int num = engine->program.time_sig_num_map[section];
    int den = engine->program.time_sig_den_map[section];
    if (num <= 0 || den <= 0) {
        return len;
    }
    if (16 % den != 0) {
        return len;
    }
    int steps_per_beat = 16 / den;
    int bar_steps = num * steps_per_beat;
    if (bar_steps <= 0) {
        return len;
    }
    int rem = len % bar_steps;
    if (rem == 0) {
        return len;
    }
    return len + (bar_steps - rem);
}

static float osc_sample(Voice *voice) {
    float sample = 0.0f;
    switch (voice->type) {
        case SYNTH_SINE:
            sample = sinf(voice->phase);
            break;
        case SYNTH_SAW: {
            float x = voice->phase / (2.0f * (float)M_PI);
            sample = 2.0f * (x - floorf(x + 0.5f));
            break;
        }
        case SYNTH_SQUARE:
            sample = (voice->phase < (float)M_PI) ? 1.0f : -1.0f;
            break;
        case SYNTH_TRI: {
            float x = voice->phase / (2.0f * (float)M_PI);
            float saw = 2.0f * (x - floorf(x + 0.5f));
            sample = 2.0f * fabsf(saw) - 1.0f;
            break;
        }
        case SYNTH_NOISE: {
            voice->rng = voice->rng * 1664525u + 1013904223u;
            sample = ((voice->rng >> 8) / 8388608.0f) - 1.0f;
            break;
        }
        case SYNTH_PULSE: {
            float duty = 0.3f;
            sample = (voice->phase < (float)M_PI * 2.0f * duty) ? 1.0f : -1.0f;
            break;
        }
        case SYNTH_FM: {
            float mod = sinf(voice->phase * 2.0f);
            sample = sinf(voice->phase + mod * 2.5f);
            break;
        }
        case SYNTH_FM2: {
            float mod1 = sinf(voice->phase * 3.0f);
            float mod2 = sinf(voice->phase * 7.0f + mod1 * 2.0f);
            sample = sinf(voice->phase + mod2 * 3.0f);
            break;
        }
        case SYNTH_RING: {
            float x = voice->phase / (2.0f * (float)M_PI);
            float saw = 2.0f * (x - floorf(x + 0.5f));
            sample = sinf(voice->phase) * saw;
            break;
        }
        case SYNTH_ACID: {
            float x = voice->phase / (2.0f * (float)M_PI);
            sample = 2.0f * (x - floorf(x + 0.5f));
            break;
        }
        case SYNTH_KICK:
        case SYNTH_KICK808:
        case SYNTH_KICK909: {
            float drop = 1.0f + voice->pitch_env * 4.2f;
            sample = sinf(voice->phase * drop);
            break;
        }
        case SYNTH_TOM: {
            float drop = 1.0f + voice->pitch_env * 1.5f;
            sample = sinf(voice->phase * drop);
            break;
        }
        case SYNTH_SNARE:
        case SYNTH_SNARE808:
        case SYNTH_SNARE909:
        case SYNTH_CLAP:
        case SYNTH_CLAP909:
        case SYNTH_RIM: {
            voice->rng = voice->rng * 1664525u + 1013904223u;
            float n = ((voice->rng >> 8) / 8388608.0f) - 1.0f;
            sample = n;
            break;
        }
        case SYNTH_HAT_C:
        case SYNTH_HAT_O:
        case SYNTH_HAT808:
        case SYNTH_HAT909: {
            voice->rng = voice->rng * 1664525u + 1013904223u;
            float n = ((voice->rng >> 8) / 8388608.0f) - 1.0f;
            float m1 = sinf(voice->phase * 2.2f);
            float m2 = sinf(voice->phase * 3.4f);
            float m3 = sinf(voice->phase * 5.1f);
            float m4 = sinf(voice->phase * 8.0f);
            sample = n * 0.5f + (m1 + m2 + m3 + m4) * 0.1f;
            break;
        }
        case SYNTH_GLITCH: {
            voice->rng = voice->rng * 1103515245u + 12345u;
            float n = ((voice->rng >> 8) / 8388608.0f) - 1.0f;
            float stepped = floorf(n * 6.0f) / 6.0f;
            sample = stepped * (sinf(voice->phase * 4.0f) * 0.6f + 0.4f);
            break;
        }
        case SYNTH_METAL: {
            float a = sinf(voice->phase * 2.0f);
            float b = sinf(voice->phase * 3.0f + a * 1.5f);
            float c = sinf(voice->phase * 5.0f + b * 1.2f);
            sample = (a + b + c) * 0.33f;
            break;
        }
        case SYNTH_BITPERC: {
            voice->rng = voice->rng * 1664525u + 1013904223u;
            float n = ((voice->rng >> 8) / 8388608.0f) - 1.0f;
            float crushed = floorf(n * 8.0f) / 8.0f;
            sample = crushed;
            break;
        }
        case SYNTH_COMB:
        case SYNTH_PM_STRING:
        case SYNTH_PM_BELL:
        case SYNTH_PM_PIPE:
        case SYNTH_PM_KICK:
        case SYNTH_PM_SNARE:
        case SYNTH_PM_HAT:
        case SYNTH_PM_CLAP:
        case SYNTH_PM_TOM: {
            float input = 0.0f;
            if (voice->age < 96) {
                float excite = 1.0f - (float)voice->age / 96.0f;
                if (voice->type == SYNTH_PM_BELL) {
                    input = sinf(voice->phase * 6.0f) * voice->amp * excite;
                } else if (voice->type == SYNTH_PM_KICK) {
                    input = sinf(voice->phase * 1.1f) * voice->amp * (0.8f + excite);
                } else if (voice->type == SYNTH_PM_SNARE) {
                    voice->rng = voice->rng * 1664525u + 1013904223u;
                    input = (((voice->rng >> 8) / 8388608.0f) - 1.0f) * voice->amp * (0.7f + excite);
                } else if (voice->type == SYNTH_PM_HAT) {
                    voice->rng = voice->rng * 1664525u + 1013904223u;
                    float n = (((voice->rng >> 8) / 8388608.0f) - 1.0f);
                    float m1 = sinf(voice->phase * 2.8f);
                    float m2 = sinf(voice->phase * 5.3f);
                    float m3 = sinf(voice->phase * 9.1f);
                    input = (n * 0.65f + (m1 + m2 + m3) * 0.14f) * voice->amp * (0.7f + excite);
                } else if (voice->type == SYNTH_PM_CLAP) {
                    voice->rng = voice->rng * 1664525u + 1013904223u;
                    float n = (((voice->rng >> 8) / 8388608.0f) - 1.0f);
                    float m1 = sinf(voice->phase * 3.6f);
                    float m2 = sinf(voice->phase * 6.7f);
                    input = (n * 0.55f + (m1 + m2) * 0.16f) * voice->amp * (0.7f + excite);
                } else if (voice->type == SYNTH_PM_TOM) {
                    input = sinf(voice->phase * 1.6f) * voice->amp * (0.7f + excite);
                } else if (voice->type == SYNTH_PM_PIPE) {
                    input = sinf(voice->phase * 2.0f) * voice->amp * excite;
                } else {
                    voice->rng = voice->rng * 1664525u + 1013904223u;
                    input = (((voice->rng >> 8) / 8388608.0f) - 1.0f) * voice->amp * excite;
                }
            }
            float y = voice->comb_buf[voice->comb_idx];
            voice->comb_state = (1.0f - voice->comb_damp) * y + voice->comb_damp * voice->comb_state;
            voice->comb_buf[voice->comb_idx] = input + voice->comb_state * voice->comb_feedback;
            voice->comb_idx = (voice->comb_idx + 1) % voice->comb_len;
            sample = voice->comb_state;
            break;
        }
    }

    return sample;
}

static float svf_lpf(Voice *v, float input, float cutoff_hz, float resonance, double sample_rate) {
    float f = 2.0f * sinf((float)M_PI * fminf(cutoff_hz, (float)sample_rate * 0.45f) / (float)sample_rate);
    float q = fmaxf(0.1f, 1.0f - resonance);
    v->svf_lp += f * v->svf_bp;
    float hp = input - v->svf_lp - q * v->svf_bp;
    v->svf_bp += f * hp;
    return v->svf_lp;
}

static float one_pole_lp(float input, float cutoff_hz, double sample_rate, float *state) {
    float alpha = expf(-2.0f * (float)M_PI * fminf(cutoff_hz, (float)sample_rate * 0.45f) / (float)sample_rate);
    *state = (1.0f - alpha) * input + alpha * (*state);
    return *state;
}

static float one_pole_hp(float input, float cutoff_hz, double sample_rate, float *state) {
    float lp = one_pole_lp(input, cutoff_hz, sample_rate, state);
    return input - lp;
}

static void voice_note_on(Voice *voice,
                          const SynthDef *synth,
                          float freq,
                          double sample_rate,
                          int gate_samples,
                          float amp_scale,
                          int glide_samples,
                          int accent) {
    voice->active = true;
    voice->type = synth->type;
    voice->age = 0;
    voice->pitch_env = 1.0f;
    voice->pitch_decay = (float)(1.0 / (0.03 * sample_rate));
    voice->hp_state = 0.0f;
    voice->svf_lp = 0.0f;
    voice->svf_bp = 0.0f;
    if (glide_samples > 0) {
        voice->target_freq = freq;
        voice->glide_samples = glide_samples;
        voice->glide_step = (freq - voice->freq) / (float)glide_samples;
    } else {
        voice->target_freq = freq;
        voice->glide_samples = 0;
        voice->glide_step = 0.0f;
        voice->freq = freq;
    }
    if (voice->type == SYNTH_HAT_C || voice->type == SYNTH_HAT_O ||
        voice->type == SYNTH_HAT808 || voice->type == SYNTH_HAT909) {
        voice->freq = (voice->type == SYNTH_HAT808) ? 7000.0f : 9000.0f;
        voice->target_freq = voice->freq;
        voice->glide_samples = 0;
        voice->glide_step = 0.0f;
    }
    voice->phase = 0.0f;
    voice->env = 0.0f;
    voice->sus = synth->sus;
    voice->stage = ENV_ATTACK;
    voice->gate_samples = gate_samples;
    voice->cutoff = synth->cutoff;
    voice->filter_state = 0.0f;
    voice->rng ^= (uint32_t)(freq * 1000.0f);
    voice->res = synth->res;
    voice->accent = accent ? 1.0f : 0.0f;
    voice->svf_lp = 0.0f;
    voice->svf_bp = 0.0f;
    voice->crush_hold = 0.0f;
    voice->crush_count = 0;
    if (voice->type == SYNTH_COMB) {
        voice->amp = synth->comb_excite * amp_scale;
    } else {
        voice->amp = synth->amp * amp_scale;
    }
    if (voice->type == SYNTH_COMB || voice->type == SYNTH_PM_STRING || voice->type == SYNTH_PM_BELL || voice->type == SYNTH_PM_PIPE ||
        voice->type == SYNTH_PM_KICK || voice->type == SYNTH_PM_SNARE || voice->type == SYNTH_PM_HAT ||
        voice->type == SYNTH_PM_CLAP || voice->type == SYNTH_PM_TOM) {
        if (voice->type == SYNTH_PM_KICK) {
            voice->amp *= 1.9f;
        } else if (voice->type == SYNTH_PM_TOM) {
            voice->amp *= 1.7f;
        } else if (voice->type == SYNTH_PM_HAT) {
            voice->amp *= 1.8f;
        } else if (voice->type == SYNTH_PM_SNARE || voice->type == SYNTH_PM_CLAP) {
            voice->amp *= 1.6f;
        } else {
            voice->amp *= 1.5f;
        }
        if (voice->type == SYNTH_PM_KICK) {
            voice->freq = 60.0f;
            voice->target_freq = voice->freq;
            voice->glide_samples = 0;
            voice->glide_step = 0.0f;
        } else if (voice->type == SYNTH_PM_SNARE) {
            voice->freq = 180.0f;
            voice->target_freq = voice->freq;
            voice->glide_samples = 0;
            voice->glide_step = 0.0f;
        } else if (voice->type == SYNTH_PM_HAT) {
            voice->freq = 9000.0f;
            voice->target_freq = voice->freq;
            voice->glide_samples = 0;
            voice->glide_step = 0.0f;
        } else if (voice->type == SYNTH_PM_CLAP) {
            voice->freq = 240.0f;
            voice->target_freq = voice->freq;
            voice->glide_samples = 0;
            voice->glide_step = 0.0f;
        } else if (voice->type == SYNTH_PM_TOM) {
            voice->freq = 120.0f;
            voice->target_freq = voice->freq;
            voice->glide_samples = 0;
            voice->glide_step = 0.0f;
        }
        int len = (int)(sample_rate / fmaxf(freq, 40.0f));
        if (voice->type == SYNTH_PM_KICK) {
            len = (int)(sample_rate / 55.0f);
        } else if (voice->type == SYNTH_PM_SNARE) {
            len = (int)(sample_rate / 180.0f);
        } else if (voice->type == SYNTH_PM_HAT) {
            len = (int)(sample_rate / 7000.0f);
        } else if (voice->type == SYNTH_PM_CLAP) {
            len = (int)(sample_rate / 260.0f);
        } else if (voice->type == SYNTH_PM_TOM) {
            len = (int)(sample_rate / 120.0f);
        }
        if (len < 8) len = 8;
        if (len > COMB_MAX_SAMPLES) len = COMB_MAX_SAMPLES;
        voice->comb_len = len;
        voice->comb_idx = 0;
        voice->comb_feedback = synth->comb_feedback;
        voice->comb_damp = synth->comb_damp;
        if (voice->type == SYNTH_PM_STRING) {
            voice->comb_feedback = 0.88f;
            voice->comb_damp = 0.18f;
        } else if (voice->type == SYNTH_PM_BELL) {
            voice->comb_feedback = 0.94f;
            voice->comb_damp = 0.12f;
        } else if (voice->type == SYNTH_PM_PIPE) {
            voice->comb_feedback = 0.96f;
            voice->comb_damp = 0.06f;
        } else if (voice->type == SYNTH_PM_KICK) {
            voice->comb_feedback = 0.95f;
            voice->comb_damp = 0.06f;
        } else if (voice->type == SYNTH_PM_SNARE) {
            voice->comb_feedback = 0.88f;
            voice->comb_damp = 0.28f;
        } else if (voice->type == SYNTH_PM_HAT) {
            voice->comb_feedback = 0.75f;
            voice->comb_damp = 0.6f;
        } else if (voice->type == SYNTH_PM_CLAP) {
            voice->comb_feedback = 0.86f;
            voice->comb_damp = 0.3f;
        } else if (voice->type == SYNTH_PM_TOM) {
            voice->comb_feedback = 0.92f;
            voice->comb_damp = 0.12f;
        }

        // Default PM material: wood (slightly more damp, slightly less feedback).
        if (voice->type == SYNTH_PM_STRING || voice->type == SYNTH_PM_BELL || voice->type == SYNTH_PM_PIPE ||
            voice->type == SYNTH_PM_KICK || voice->type == SYNTH_PM_SNARE || voice->type == SYNTH_PM_HAT ||
            voice->type == SYNTH_PM_CLAP || voice->type == SYNTH_PM_TOM) {
            voice->comb_feedback = fmaxf(0.5f, voice->comb_feedback * 0.93f);
            voice->comb_damp = fminf(0.85f, voice->comb_damp + 0.08f);
        }
        if (voice->type == SYNTH_PM_BELL || voice->type == SYNTH_PM_PIPE) {
            voice->comb_feedback = fmaxf(0.5f, voice->comb_feedback * 0.9f);
            voice->comb_damp = fminf(0.9f, voice->comb_damp + 0.1f);
        }
        voice->comb_state = 0.0f;
        for (int i = 0; i < voice->comb_len; i++) {
            voice->comb_buf[i] = 0.0f;
        }
    }

    float atk = synth->atk;
    float dec = synth->dec;
    float rel = synth->rel;
    if (voice->type == SYNTH_KICK || voice->type == SYNTH_KICK808 || voice->type == SYNTH_KICK909) {
        atk = 0.001f; dec = (voice->type == SYNTH_KICK909 ? 0.18f : 0.26f);
        voice->sus = 0.0f; rel = 0.05f;
        voice->pitch_decay = (float)(1.0 / ((voice->type == SYNTH_KICK909) ? 0.03 : 0.045) * sample_rate);
    } else if (voice->type == SYNTH_TOM) {
        atk = 0.001f; dec = 0.18f; voice->sus = 0.0f; rel = 0.05f;
        voice->pitch_decay = (float)(1.0 / (0.06 * sample_rate));
    } else if (voice->type == SYNTH_SNARE || voice->type == SYNTH_SNARE808 || voice->type == SYNTH_SNARE909) {
        atk = 0.001f; dec = (voice->type == SYNTH_SNARE909 ? 0.045f : 0.06f);
        voice->sus = 0.0f; rel = 0.03f;
    } else if (voice->type == SYNTH_CLAP || voice->type == SYNTH_CLAP909) {
        atk = 0.001f; dec = (voice->type == SYNTH_CLAP909 ? 0.06f : 0.07f);
        voice->sus = 0.0f; rel = 0.04f;
    } else if (voice->type == SYNTH_HAT_C || voice->type == SYNTH_HAT808) {
        atk = 0.001f; dec = 0.018f; voice->sus = 0.0f; rel = 0.012f;
    } else if (voice->type == SYNTH_HAT_O || voice->type == SYNTH_HAT909) {
        atk = 0.001f; dec = 0.07f; voice->sus = 0.0f; rel = 0.045f;
    } else if (voice->type == SYNTH_RIM || voice->type == SYNTH_GLITCH || voice->type == SYNTH_BITPERC) {
        atk = 0.001f; dec = 0.03f; voice->sus = 0.0f; rel = 0.02f;
    } else if (voice->type == SYNTH_METAL) {
        atk = 0.002f; dec = 0.12f; voice->sus = 0.0f; rel = 0.06f;
    } else if (voice->type == SYNTH_PM_KICK) {
        atk = 0.001f; dec = 0.2f; voice->sus = 0.0f; rel = 0.08f;
        voice->pitch_decay = (float)(1.0 / (0.05 * sample_rate));
    } else if (voice->type == SYNTH_PM_SNARE) {
        atk = 0.001f; dec = 0.07f; voice->sus = 0.0f; rel = 0.04f;
    } else if (voice->type == SYNTH_PM_HAT) {
        atk = 0.001f; dec = 0.03f; voice->sus = 0.0f; rel = 0.02f;
    } else if (voice->type == SYNTH_PM_CLAP) {
        atk = 0.001f; dec = 0.06f; voice->sus = 0.0f; rel = 0.04f;
    } else if (voice->type == SYNTH_PM_TOM) {
        atk = 0.001f; dec = 0.14f; voice->sus = 0.0f; rel = 0.06f;
    } else if (voice->type == SYNTH_COMB || voice->type == SYNTH_PM_STRING || voice->type == SYNTH_PM_BELL || voice->type == SYNTH_PM_PIPE) {
        atk = 0.001f; dec = 0.4f; voice->sus = 0.0f; rel = 0.2f;
    }
    voice->atk_inc = atk <= 0.0001f ? 1.0f : (1.0f / (float)(atk * sample_rate));
    if (voice->accent > 0.5f && voice->type == SYNTH_ACID) {
        dec *= 0.7f;
        rel *= 0.7f;
        voice->amp *= 1.15f;
    }
    voice->dec_inc = dec <= 0.0001f ? 1.0f : ((1.0f - voice->sus) / (float)(dec * sample_rate));
    voice->rel_inc = rel <= 0.0001f ? 1.0f : (1.0f / (float)(rel * sample_rate));
}

static float voice_render(Voice *voice, double sample_rate) {
    if (!voice->active) {
        return 0.0f;
    }

    if (voice->stage == ENV_ATTACK) {
        voice->env += voice->atk_inc;
        if (voice->env >= 1.0f) {
            voice->env = 1.0f;
            voice->stage = ENV_DECAY;
        }
    } else if (voice->stage == ENV_DECAY) {
        voice->env -= voice->dec_inc;
        if (voice->env <= voice->sus) {
            voice->env = voice->sus;
            voice->stage = ENV_SUSTAIN;
        }
    } else if (voice->stage == ENV_SUSTAIN) {
        if (voice->gate_samples <= 0) {
            voice->stage = ENV_RELEASE;
        }
    } else if (voice->stage == ENV_RELEASE) {
        voice->env -= voice->rel_inc;
        if (voice->env <= 0.0f) {
            voice->env = 0.0f;
            voice->stage = ENV_OFF;
            voice->active = false;
            return 0.0f;
        }
    }

    voice->gate_samples--;

    if (voice->glide_samples > 0) {
        voice->freq += voice->glide_step;
        voice->glide_samples--;
    }
    if (voice->pitch_env > 0.0f) {
        voice->pitch_env -= voice->pitch_decay;
        if (voice->pitch_env < 0.0f) voice->pitch_env = 0.0f;
    }

    float sample = osc_sample(voice);

    float phase_inc = 2.0f * (float)M_PI * voice->freq / (float)sample_rate;
    voice->phase += phase_inc;
    if (voice->phase >= 2.0f * (float)M_PI) {
        voice->phase -= 2.0f * (float)M_PI;
    }

    float processed = sample;
    if (voice->type == SYNTH_HAT_C || voice->type == SYNTH_HAT_O || voice->type == SYNTH_HAT808 || voice->type == SYNTH_HAT909 ||
        voice->type == SYNTH_PM_HAT || voice->type == SYNTH_PM_SNARE || voice->type == SYNTH_PM_CLAP ||
        voice->type == SYNTH_RIM || voice->type == SYNTH_SNARE || voice->type == SYNTH_SNARE808 || voice->type == SYNTH_SNARE909 ||
        voice->type == SYNTH_CLAP || voice->type == SYNTH_CLAP909 || voice->type == SYNTH_BITPERC) {
        processed = one_pole_hp(processed, 1200.0f, sample_rate, &voice->hp_state);
    }

    // Acid: 303-ish resonant low-pass with envelope modulation.
    if (voice->type == SYNTH_ACID) {
        float env_depth = 2600.0f + voice->accent * 800.0f;
        float cutoff = voice->cutoff + voice->env * env_depth + voice->accent * 200.0f;
        float res = fminf(0.97f, voice->res + voice->accent * 0.1f);
        processed = svf_lpf(voice, processed, cutoff, res, sample_rate);
        processed = svf_lpf(voice, processed, cutoff, res, sample_rate); // 2x oversample approx
        processed = tanhf(processed * (2.0f + voice->accent * 0.55f));
    } else if (voice->type == SYNTH_SNARE || voice->type == SYNTH_SNARE808 || voice->type == SYNTH_SNARE909 || voice->type == SYNTH_PM_SNARE) {
        float band = one_pole_lp(processed, 2400.0f, sample_rate, &voice->filter_state);
        float tone = sinf(voice->phase * 0.5f);
        processed = band * 0.55f + tone * 0.45f;
    } else if (voice->type == SYNTH_CLAP || voice->type == SYNTH_CLAP909 || voice->type == SYNTH_PM_CLAP) {
        float band = one_pole_lp(processed, 2800.0f, sample_rate, &voice->filter_state);
        float t = (float)voice->age / (float)sample_rate;
        float gate = (t < 0.006f || (t > 0.012f && t < 0.02f) || (t > 0.026f && t < 0.034f)) ? 1.0f : 0.2f;
        processed = band * gate;
    } else if (voice->type == SYNTH_HAT_C || voice->type == SYNTH_HAT_O || voice->type == SYNTH_HAT808 || voice->type == SYNTH_HAT909 ||
               voice->type == SYNTH_PM_HAT) {
        float band = one_pole_lp(processed, 9000.0f, sample_rate, &voice->filter_state);
        processed = band;
    } else {
        float alpha = expf(-2.0f * (float)M_PI * voice->cutoff / (float)sample_rate);
        voice->filter_state = (1.0f - alpha) * processed + alpha * voice->filter_state;
        processed = voice->filter_state;
    }

    if (is_pm_type(voice->type)) {
        processed = tanhf(processed * 1.6f);
        if (voice->type == SYNTH_PM_KICK || voice->type == SYNTH_PM_TOM) {
            processed = one_pole_lp(processed, 1800.0f, sample_rate, &voice->filter_state);
        } else if (voice->type == SYNTH_PM_SNARE || voice->type == SYNTH_PM_CLAP) {
            float hp = one_pole_hp(processed, 800.0f, sample_rate, &voice->hp_state);
            processed = one_pole_lp(hp, 3800.0f, sample_rate, &voice->filter_state);
        } else if (voice->type == SYNTH_PM_HAT) {
            float hp = one_pole_hp(processed, 5000.0f, sample_rate, &voice->hp_state);
            processed = one_pole_lp(hp, 12000.0f, sample_rate, &voice->filter_state);
        } else {
            float hp = one_pole_hp(processed, 400.0f, sample_rate, &voice->hp_state);
            processed = one_pole_lp(hp, 3800.0f, sample_rate, &voice->filter_state);
        }

        // Tight, sci-fi edge: transient focus + light sample-hold.
        float t_ms = (float)voice->age / (float)sample_rate * 1000.0f;
        float transient = 1.0f + 0.45f * expf(-t_ms / 12.0f);
        processed *= transient;
        int hold = is_pm_drum(voice->type) ? 2 : 3;
        if (voice->crush_count <= 0) {
            voice->crush_hold = processed;
            voice->crush_count = hold;
        }
        processed = voice->crush_hold;
        voice->crush_count--;

        if (is_pm_drum(voice->type)) {
            processed = floorf(processed * 128.0f) / 128.0f;
        }
    }

    voice->age++;
    return processed * voice->env * voice->amp;
}

static int track_cycle_steps(const TrackRuntime *track, const PatternDef *pattern) {
    if (!pattern) {
        return 0;
    }
    int base_len = effective_pattern_length(&g_engine, pattern);
    if (track->palindrome && base_len > 1) {
        base_len = base_len * 2 - 2;
    }
    if (track->iter > 1) {
        base_len *= track->iter;
    }
    return base_len;
}

static const PatternDef *sequence_current_pattern(EngineState *engine, TrackRuntime *track) {
    if (!track->sequence || track->sequence->count == 0) {
        return track->pattern;
    }
    if (track->seq_start > 0) {
        if (track->seq_end < 0) {
            track->seq_end = track->sequence->count;
        }
        int start = track->seq_start - 1;
        int end = track->seq_end - 1;
        if (track->seq_pos < start || track->seq_pos > end) {
            return NULL;
        }
    }
    const SequenceStep *step = &track->sequence->steps[track->seq_index];
    int idx = dsl_find_pattern(&engine->program, step->pattern);
    if (idx < 0) {
        return NULL;
    }
    return &engine->program.patterns[idx];
}

static void update_track_tempo(EngineState *engine, TrackRuntime *track);
static void update_all_track_tempos(EngineState *engine);

static void advance_sequence(TrackRuntime *track) {
    if (!track->sequence || track->sequence->count == 0) {
        return;
    }
    const SequenceStep *step = &track->sequence->steps[track->seq_index];
    track->seq_repeat_done++;
    if (track->seq_repeat_done >= step->repeat) {
        track->seq_repeat_done = 0;
        track->seq_index = (track->seq_index + 1) % track->sequence->count;
        track->seq_pos = (track->seq_pos + 1) % track->sequence->count;
        g_engine.pattern_epoch++;
        if (track->is_tempo_leader) {
            int max_section = track->sequence->count;
            if (max_section < 1) max_section = 14;
            g_engine.tempo_section = (track->seq_pos % max_section) + 1;
            update_all_track_tempos(&g_engine);
        } else {
            update_track_tempo(&g_engine, track);
        }
    }
}

static void update_track_tempo(EngineState *engine, TrackRuntime *track) {
    int idx = engine->tempo_section;
    if (idx < 1 || idx > 14) {
        return;
    }
    float map = engine->program.tempo_map[idx];
    if (map <= 0.0f) {
        map = 1.0f;
    }
    float mult = track->base_rate * map;
    int sps = (int)((float)engine->base_samples_per_step / mult);
    if (sps < 1) sps = 1;
    track->samples_per_step = sps;
}

static void update_all_track_tempos(EngineState *engine) {
    for (int i = 0; i < engine->track_count; i++) {
        update_track_tempo(engine, &engine->tracks[i]);
    }
}

static int track_active_for_sequence(TrackRuntime *track) {
    if (!track->sequence || track->sequence->count == 0) {
        return 1;
    }
    if (track->seq_start <= 0) {
        return 1;
    }
    if (track->seq_end < 0) {
        return 1;
    }
    int start = track->seq_start - 1;
    int end = track->seq_end - 1;
    return (track->seq_pos >= start && track->seq_pos <= end);
}

static void schedule_track_step(EngineState *engine, TrackRuntime *track) {
    if (!track_active_for_sequence(track)) {
        if (track->sequence && track->sequence->count > 0) {
            const PatternDef *p = sequence_current_pattern(engine, track);
            if (p) {
                int cycle_steps = track_cycle_steps(track, p);
                if (cycle_steps > 0) {
                    track->step_index++;
                    if (track->step_index >= cycle_steps) {
                        track->step_index = 0;
                        advance_sequence(track);
                    }
                }
            }
        }
        return;
    }

    const PatternDef *pattern = sequence_current_pattern(engine, track);
    if (!pattern || pattern->length == 0) {
        return;
    }

    int effective_len = effective_pattern_length(engine, pattern);
    if (effective_len <= 0) {
        return;
    }

    int step = track->step_index;
    int base_step = step;
    if (track->iter > 1) {
        base_step = step / track->iter;
    }

    int idx = 0;
    if (track->palindrome && effective_len > 1) {
        int pal_len = effective_len * 2 - 2;
        int p = base_step % pal_len;
        if (p >= effective_len) {
            p = pal_len - p;
        }
        idx = p;
    } else {
        idx = base_step % effective_len;
    }

    if (track->rev) {
        idx = (effective_len - 1) - idx;
    }

    if (track->chunk > 0) {
        int chunk_count = track->chunk;
        if (chunk_count < 1) chunk_count = 1;
        int chunk_size = (effective_len + chunk_count - 1) / chunk_count;
        int cycle = (base_step / effective_len) % chunk_count;
        int chunk_start = cycle * chunk_size;
        int chunk_end = chunk_start + chunk_size - 1;
        if (idx < chunk_start || idx > chunk_end) {
            track->step_index++;
            return;
        }
    }

    int do_play = 1;
    if (track->every > 1 && (step % track->every) != 0) {
        do_play = 0;
    }

    if (do_play && track->density < 1.0f) {
        track->rng ^= track->rng << 13;
        track->rng ^= track->rng >> 17;
        track->rng ^= track->rng << 5;
        float r = (track->rng & 0xFFFFFF) / 16777215.0f;
        if (r > track->density) {
            do_play = 0;
        }
    }

    if (do_play && idx < pattern->length) {
        int note = pattern->notes[idx];
        if (note >= 0) {
            float cents = pattern->cents[idx];
            float midi = (float)note + (cents / 100.0f);
            float freq = 440.0f * powf(2.0f, (midi - 69.0f) / 12.0f);
            float slide_ms = track->slide_ms;
            if (pattern->slide_ms[idx] >= 0.0f) {
                slide_ms = pattern->slide_ms[idx];
            }
            int glide_samples = 0;
            if (slide_ms > 0.0f) {
                glide_samples = (int)(engine->sample_rate * (slide_ms / 1000.0f));
            }
            int accent = pattern->accent[idx];
            if (!accent && track->accent_prob > 0.0f) {
                track->rng ^= track->rng << 13;
                track->rng ^= track->rng >> 17;
                track->rng ^= track->rng << 5;
                float r = (track->rng & 0xFFFFFF) / 16777215.0f;
                if (r <= track->accent_prob) {
                    accent = 1;
                }
            }
            for (int v = 0; v < MAX_VOICES; v++) {
                if (!engine->voices[v].active) {
                    int gate = (int)(track->samples_per_step * 0.9f);
                    voice_note_on(&engine->voices[v], track->synth, freq, engine->sample_rate, gate, 1.0f, glide_samples, accent);
                    break;
                }
            }

            if (track->ornament_prob > 0.0f && pattern->degree_valid[idx]) {
                track->rng ^= track->rng << 13;
                track->rng ^= track->rng >> 17;
                track->rng ^= track->rng << 5;
                float r = (track->rng & 0xFFFFFF) / 16777215.0f;
                if (r <= track->ornament_prob) {
                    int deg = pattern->degree[idx];
                    int oct = pattern->degree_octave[idx];
                    int micro = pattern->degree_micro[idx];
                    int grace_dir = -1;
                    if (track->ornament_mode == 1) grace_dir = 1;
                    else if (track->ornament_mode == 2) {
                        grace_dir = track->ornament_alt ? 1 : -1;
                        track->ornament_alt = !track->ornament_alt;
                    }

                    int grace_deg = deg + grace_dir;
                    int grace_oct = oct;
                    if (grace_deg < 1) {
                        grace_deg = 7;
                        grace_oct -= 1;
                    } else if (grace_deg > 7) {
                        grace_deg = 1;
                        grace_oct += 1;
                    }
                    float grace_cents = engine->program.maqam_offsets[grace_deg - 1] + (micro * 50.0f);
                    float grace_midi = engine->program.root_midi + grace_oct * 12 + (grace_cents / 100.0f);
                    float grace_freq = 440.0f * powf(2.0f, (grace_midi - 69.0f) / 12.0f);

                    for (int v = 0; v < MAX_VOICES; v++) {
                        if (!engine->voices[v].active) {
                            int gate = (int)(track->samples_per_step * 0.2f);
                            voice_note_on(&engine->voices[v], track->synth, grace_freq, engine->sample_rate, gate, 0.5f, 0, 0);
                            break;
                        }
                    }
                }
            }

            if (track->stut > 1) {
                track->stut_remaining = track->stut - 1;
                track->stut_samples_per = track->samples_per_step / track->stut;
                if (track->stut_samples_per < 1) {
                    track->stut_samples_per = 1;
                }
                track->stut_samples_until = track->stut_samples_per;
                track->stut_freq = freq;
            } else {
                track->stut_remaining = 0;
            }
        }
    }

    track->step_index++;

    int cycle_steps = track_cycle_steps(track, pattern);
    if (cycle_steps > 0 && track->step_index >= cycle_steps) {
        track->step_index = 0;
        if (track->sequence) {
            advance_sequence(track);
        }
    }
}

static OSStatus render_callback(void *in_ref_con,
                                AudioUnitRenderActionFlags *io_action_flags,
                                const AudioTimeStamp *in_time_stamp,
                                UInt32 in_bus_number,
                                UInt32 in_number_frames,
                                AudioBufferList *io_data) {
    (void)io_action_flags;
    (void)in_time_stamp;
    (void)in_bus_number;

    EngineState *engine = (EngineState *)in_ref_con;
    bool interleaved = (io_data->mNumberBuffers == 1);
    float *out_l = (float *)io_data->mBuffers[0].mData;
    float *out_r = interleaved ? NULL : (float *)io_data->mBuffers[1].mData;

    float rms_l = 0.0f;
    float rms_r = 0.0f;
    float peak_l = 0.0f;
    float peak_r = 0.0f;
    int clip = 0;

    for (UInt32 frame = 0; frame < in_number_frames; frame++) {
        for (int t = 0; t < engine->track_count; t++) {
            TrackRuntime *track = &engine->tracks[t];
            if (track->samples_until_step <= 0) {
                schedule_track_step(engine, track);
                track->samples_until_step = track->samples_per_step;
            }
            track->samples_until_step--;

            if (track->stut_remaining > 0) {
                track->stut_samples_until--;
                if (track->stut_samples_until <= 0) {
                    for (int v = 0; v < MAX_VOICES; v++) {
                        if (!engine->voices[v].active) {
                            int gate = (int)(track->stut_samples_per * 0.8f);
                            voice_note_on(&engine->voices[v], track->synth, track->stut_freq, engine->sample_rate, gate, 1.0f, 0, 0);
                            break;
                        }
                    }
                    track->stut_remaining--;
                    track->stut_samples_until = track->stut_samples_per;
                }
            }
        }

        float mix = 0.0f;
        for (int v = 0; v < MAX_VOICES; v++) {
            mix += voice_render(&engine->voices[v], engine->sample_rate);
        }
        mix *= engine->program.master_amp;
        if (engine->bit_depth == 16) {
            mix = floorf(mix * 32767.0f) / 32767.0f;
        } else if (engine->bit_depth == 24) {
            mix = floorf(mix * 8388607.0f) / 8388607.0f;
        }

        float absMix = fabsf(mix);
        if (absMix > 1.0f) {
            clip = 1;
        }
        if (interleaved) {
            out_l[frame * 2] = mix;
            out_l[frame * 2 + 1] = mix;
            if (absMix > peak_l) peak_l = absMix;
            if (absMix > peak_r) peak_r = absMix;
        } else {
            out_l[frame] = mix;
            out_r[frame] = mix;
            if (absMix > peak_l) peak_l = absMix;
            if (absMix > peak_r) peak_r = absMix;
        }

        rms_l += mix * mix;
        rms_r += mix * mix;
    }

    rms_l = sqrtf(rms_l / (float)in_number_frames);
    rms_r = sqrtf(rms_r / (float)in_number_frames);
    engine->meter_l = rms_l;
    engine->meter_r = rms_r;
    engine->meter_peak_l = peak_l;
    engine->meter_peak_r = peak_r;
    engine->meter_clip = clip;

    return noErr;
}

static int build_runtime(EngineState *engine) {
    engine->track_count = 0;
    int tempo_leader_set = 0;

    for (int i = 0; i < engine->program.track_count; i++) {
        TrackDef *track = &engine->program.tracks[i];
        int pattern_idx = -1;
        int sequence_idx = -1;
        if (track->is_sequence) {
            sequence_idx = dsl_find_sequence(&engine->program, track->pattern);
            if (sequence_idx < 0) {
                return 0;
            }
            const SequenceDef *seq = &engine->program.sequences[sequence_idx];
            for (int s = 0; s < seq->count; s++) {
                if (dsl_find_pattern(&engine->program, seq->steps[s].pattern) < 0) {
                    return 0;
                }
            }
        } else {
            pattern_idx = dsl_find_pattern(&engine->program, track->pattern);
            if (pattern_idx < 0) {
                return 0;
            }
        }
        int synth_idx = dsl_find_synth(&engine->program, track->synth);
        if (synth_idx < 0) {
            return 0;
        }
        TrackRuntime *runtime = &engine->tracks[engine->track_count++];
        runtime->pattern = (pattern_idx >= 0) ? &engine->program.patterns[pattern_idx] : NULL;
        runtime->synth = &engine->program.synths[synth_idx];
        runtime->sequence = (sequence_idx >= 0) ? &engine->program.sequences[sequence_idx] : NULL;
        runtime->step_index = 0;
        runtime->every = track->every;
        runtime->rev = track->rev;
        runtime->palindrome = track->palindrome;
        runtime->iter = track->iter;
        runtime->chunk = track->chunk;
        runtime->stut = track->stut;
        runtime->density = track->density;
        runtime->rng = (uint32_t)(0x9E3779B9u + i * 2654435761u);
        runtime->stut_remaining = 0;
        runtime->stut_samples_until = 0;
        runtime->stut_samples_per = 0;
        runtime->stut_freq = 0.0f;
        runtime->seq_index = 0;
        runtime->seq_repeat_left = 0;
        runtime->seq_repeat_done = 0;
        runtime->seq_pos = 0;
        runtime->slide_ms = track->slide_ms;
        runtime->ornament_prob = track->ornament_prob;
        runtime->ornament_mode = track->ornament_mode;
        runtime->ornament_alt = 0;
        runtime->accent_prob = track->accent_prob;
        runtime->seq_start = track->seq_start;
        runtime->seq_end = track->seq_end;
        runtime->seq_pos = 0;
        runtime->seq_cycle_active = 0;
        runtime->is_tempo_leader = 0;

        float mult = track->rate * track->hurry;
        if (track->fast > 1) {
            mult *= (float)track->fast;
        }
        if (track->slow > 1) {
            mult /= (float)track->slow;
        }
        if (mult <= 0.001f) {
            mult = 0.001f;
        }

        runtime->base_rate = mult;
        runtime->samples_per_step = (int)((float)engine->base_samples_per_step / mult);
        if (runtime->samples_per_step < 1) {
            runtime->samples_per_step = 1;
        }
        runtime->samples_until_step = 0;

        if (!tempo_leader_set && runtime->sequence && runtime->sequence->count > 0) {
            runtime->is_tempo_leader = 1;
            tempo_leader_set = 1;
        }

        update_track_tempo(engine, runtime);
    }

    return 1;
}

static int start_audio_unit(EngineState *engine) {
    AudioComponentDescription desc = {0};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(NULL, &desc);
    if (!comp) {
        return 0;
    }

    OSStatus status = AudioComponentInstanceNew(comp, &engine->audio_unit);
    if (status != noErr) {
        return 0;
    }

    if (engine->output_device_id != 0) {
        AudioDeviceID dev = (AudioDeviceID)engine->output_device_id;
        status = AudioUnitSetProperty(engine->audio_unit,
                                      kAudioOutputUnitProperty_CurrentDevice,
                                      kAudioUnitScope_Global,
                                      0,
                                      &dev,
                                      sizeof(dev));
        if (status != noErr) {
            // Fallback to default device.
            engine->output_device_id = 0;
        }
    }

    if (engine->output_device_id != 0) {
        AudioDeviceID dev = (AudioDeviceID)engine->output_device_id;
        UInt32 frames = (UInt32)engine->buffer_frames;
        AudioObjectPropertyAddress addr = {
            kAudioDevicePropertyBufferFrameSize,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        AudioObjectSetPropertyData(dev, &addr, 0, NULL, sizeof(frames), &frames);

        Float64 rate = engine->sample_rate;
        AudioObjectPropertyAddress rateAddr = {
            kAudioDevicePropertyNominalSampleRate,
            kAudioObjectPropertyScopeOutput,
            kAudioObjectPropertyElementMain
        };
        AudioObjectSetPropertyData(dev, &rateAddr, 0, NULL, sizeof(rate), &rate);
    }

    UInt32 maxFrames = (UInt32)engine->buffer_frames;
    AudioUnitSetProperty(engine->audio_unit,
                         kAudioUnitProperty_MaximumFramesPerSlice,
                         kAudioUnitScope_Global,
                         0,
                         &maxFrames,
                         sizeof(maxFrames));

    AURenderCallbackStruct callback = {0};
    callback.inputProc = render_callback;
    callback.inputProcRefCon = engine;
    status = AudioUnitSetProperty(engine->audio_unit,
                                  kAudioUnitProperty_SetRenderCallback,
                                  kAudioUnitScope_Input,
                                  0,
                                  &callback,
                                  sizeof(callback));
    if (status != noErr) {
        return 0;
    }

    AudioStreamBasicDescription format = {0};
    format.mSampleRate = engine->sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = sizeof(float) * 2;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = sizeof(float) * 2;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;

    status = AudioUnitSetProperty(engine->audio_unit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0,
                                  &format,
                                  sizeof(format));
    if (status != noErr) {
        return 0;
    }

    status = AudioUnitInitialize(engine->audio_unit);
    if (status != noErr) {
        return 0;
    }

    status = AudioOutputUnitStart(engine->audio_unit);
    if (status != noErr) {
        return 0;
    }

    engine->running = true;
    return 1;
}

static void stop_audio_unit(EngineState *engine) {
    if (!engine->audio_unit) {
        return;
    }
    AudioOutputUnitStop(engine->audio_unit);
    AudioUnitUninitialize(engine->audio_unit);
    AudioComponentInstanceDispose(engine->audio_unit);
    engine->audio_unit = NULL;
    engine->running = false;
}

void audio_engine_init(void) {
    memset(&g_engine, 0, sizeof(g_engine));
    g_engine.sample_rate = 48000.0;
    g_engine.buffer_frames = 256;
    g_engine.output_device_id = 0;
    g_engine.bit_depth = 32;
    g_engine.base_samples_per_step = 1;
    g_engine.meter_l = 0.0f;
    g_engine.meter_r = 0.0f;
    g_engine.pattern_epoch = 0;

    for (int i = 0; i < MAX_VOICES; i++) {
        g_engine.voices[i].rng = (uint32_t)(0x12345678u + i * 1117u);
    }
}

void audio_engine_shutdown(void) {
    stop_audio_unit(&g_engine);
}

int audio_engine_play_script(const char *script, char *error, size_t error_len) {
    if (g_engine.running) {
        stop_audio_unit(&g_engine);
    }

    Program program;
    if (!dsl_parse_script(script, &program, error, error_len)) {
        return 0;
    }

    g_engine.program = program;
    g_engine.tempo_section = 1;
    g_engine.base_samples_per_step = (int)(g_engine.sample_rate * 60.0 / g_engine.program.tempo / 4.0);
    if (g_engine.base_samples_per_step < 1) {
        g_engine.base_samples_per_step = 1;
    }
    if (!build_runtime(&g_engine)) {
        snprintf(error, error_len, "Play command references missing synth or pattern");
        return 0;
    }

    for (int i = 0; i < MAX_VOICES; i++) {
        g_engine.voices[i].active = false;
        g_engine.voices[i].env = 0.0f;
        g_engine.voices[i].stage = ENV_OFF;
    }

    // Start drones after reset.
    for (int d = 0; d < g_engine.program.drone_count; d++) {
        DroneDef *drone = &g_engine.program.drones[d];
        int synth_idx = dsl_find_synth(&g_engine.program, drone->synth);
        if (synth_idx < 0) {
            snprintf(error, error_len, "Drone references missing synth '%s'", drone->synth);
            return 0;
        }
        float freq = 440.0f * powf(2.0f, (drone->midi - 69.0f) / 12.0f);
        for (int v = 0; v < MAX_VOICES; v++) {
            if (!g_engine.voices[v].active) {
                int gate = (int)(g_engine.sample_rate * 60.0); // long hold
                voice_note_on(&g_engine.voices[v], &g_engine.program.synths[synth_idx], freq, g_engine.sample_rate, gate, 0.6f, 0, 0);
                break;
            }
        }
    }

    if (!start_audio_unit(&g_engine)) {
        snprintf(error, error_len, "Failed to start CoreAudio output");
        return 0;
    }

    return 1;
}

int audio_engine_render_to_wav(const char *script, const char *path, double seconds, int sample_rate, int buffer_frames, char *error, size_t error_len) {
    if (!script || !path || seconds <= 0.0) {
        snprintf(error, error_len, "Invalid render parameters");
        return 0;
    }
    if (g_engine.running) {
        stop_audio_unit(&g_engine);
    }

    Program program;
    if (!dsl_parse_script(script, &program, error, error_len)) {
        return 0;
    }

    g_engine.program = program;
    g_engine.sample_rate = (double)sample_rate;
    g_engine.buffer_frames = buffer_frames;
    g_engine.tempo_section = 1;
    g_engine.base_samples_per_step = (int)(g_engine.sample_rate * 60.0 / g_engine.program.tempo / 4.0);
    if (g_engine.base_samples_per_step < 1) {
        g_engine.base_samples_per_step = 1;
    }
    if (!build_runtime(&g_engine)) {
        snprintf(error, error_len, "Render references missing synth or pattern");
        return 0;
    }

    for (int i = 0; i < MAX_VOICES; i++) {
        g_engine.voices[i].active = false;
        g_engine.voices[i].env = 0.0f;
        g_engine.voices[i].stage = ENV_OFF;
    }

    // Start drones after reset.
    for (int d = 0; d < g_engine.program.drone_count; d++) {
        DroneDef *drone = &g_engine.program.drones[d];
        int synth_idx = dsl_find_synth(&g_engine.program, drone->synth);
        if (synth_idx < 0) {
            snprintf(error, error_len, "Drone references missing synth '%s'", drone->synth);
            return 0;
        }
        float freq = 440.0f * powf(2.0f, (drone->midi - 69.0f) / 12.0f);
        for (int v = 0; v < MAX_VOICES; v++) {
            if (!g_engine.voices[v].active) {
                int gate = (int)(g_engine.sample_rate * 60.0); // long hold
                voice_note_on(&g_engine.voices[v], &g_engine.program.synths[synth_idx], freq, g_engine.sample_rate, gate, 0.6f, 0, 0);
                break;
            }
        }
    }

    AudioStreamBasicDescription outFormat = {0};
    outFormat.mSampleRate = g_engine.sample_rate;
    outFormat.mFormatID = kAudioFormatLinearPCM;
    outFormat.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    outFormat.mBytesPerPacket = sizeof(float) * 2;
    outFormat.mFramesPerPacket = 1;
    outFormat.mBytesPerFrame = sizeof(float) * 2;
    outFormat.mChannelsPerFrame = 2;
    outFormat.mBitsPerChannel = 32;

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(NULL, (const UInt8 *)path, (CFIndex)strlen(path), false);
    if (!url) {
        snprintf(error, error_len, "Invalid output path");
        return 0;
    }

    ExtAudioFileRef file = NULL;
    OSStatus status = ExtAudioFileCreateWithURL(url, kAudioFileWAVEType, &outFormat, NULL, kAudioFileFlags_EraseFile, &file);
    CFRelease(url);
    if (status != noErr || !file) {
        snprintf(error, error_len, "Failed to create output file");
        return 0;
    }

    status = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(outFormat), &outFormat);
    if (status != noErr) {
        ExtAudioFileDispose(file);
        snprintf(error, error_len, "Failed to set output format");
        return 0;
    }

    int total_frames = (int)(seconds * g_engine.sample_rate);
    int frames_per = buffer_frames > 0 ? buffer_frames : 256;
    float *buffer = (float *)calloc((size_t)frames_per * 2, sizeof(float));
    if (!buffer) {
        ExtAudioFileDispose(file);
        snprintf(error, error_len, "Out of memory");
        return 0;
    }

    AudioBufferList list = {0};
    list.mNumberBuffers = 1;
    list.mBuffers[0].mNumberChannels = 2;
    list.mBuffers[0].mDataByteSize = (UInt32)(frames_per * sizeof(float) * 2);
    list.mBuffers[0].mData = buffer;

    int rendered = 0;
    while (rendered < total_frames) {
        int batch = frames_per;
        if (rendered + batch > total_frames) {
            batch = total_frames - rendered;
        }
        list.mBuffers[0].mDataByteSize = (UInt32)(batch * sizeof(float) * 2);
        render_callback(&g_engine, NULL, NULL, 0, (UInt32)batch, &list);
        status = ExtAudioFileWrite(file, (UInt32)batch, &list);
        if (status != noErr) {
            ExtAudioFileDispose(file);
            free(buffer);
            snprintf(error, error_len, "Failed while writing audio");
            return 0;
        }
        rendered += batch;
    }

    ExtAudioFileDispose(file);
    free(buffer);
    return 1;
}

void audio_engine_stop(void) {
    stop_audio_unit(&g_engine);
}

void audio_engine_get_meter(float *out_left, float *out_right) {
    if (out_left) {
        *out_left = g_engine.meter_l;
    }
    if (out_right) {
        *out_right = g_engine.meter_r;
    }
}

void audio_engine_get_meter_ex(float *out_rms_l, float *out_rms_r, float *out_peak_l, float *out_peak_r, int *out_clip) {
    if (out_rms_l) *out_rms_l = g_engine.meter_l;
    if (out_rms_r) *out_rms_r = g_engine.meter_r;
    if (out_peak_l) *out_peak_l = g_engine.meter_peak_l;
    if (out_peak_r) *out_peak_r = g_engine.meter_peak_r;
    if (out_clip) *out_clip = g_engine.meter_clip;
}

int audio_engine_is_running(void) {
    return g_engine.running ? 1 : 0;
}

float audio_engine_get_tempo(void) {
    return g_engine.program.tempo;
}

unsigned long long audio_engine_get_pattern_epoch(void) {
    return g_engine.pattern_epoch;
}

void audio_engine_set_master(float amp) {
    if (amp < 0.0f) amp = 0.0f;
    if (amp > 4.0f) amp = 4.0f;
    g_engine.program.master_amp = amp;
}

void audio_engine_set_output_device(unsigned int device_id) {
    g_engine.output_device_id = device_id;
}

void audio_engine_set_sample_rate(double sample_rate) {
    if (sample_rate < 8000.0) sample_rate = 8000.0;
    if (sample_rate > 192000.0) sample_rate = 192000.0;
    g_engine.sample_rate = sample_rate;
}

void audio_engine_set_buffer_frames(int frames) {
    if (frames < 64) frames = 64;
    if (frames > 2048) frames = 2048;
    g_engine.buffer_frames = frames;
}

void audio_engine_set_bit_depth(int bits) {
    if (bits != 16 && bits != 24 && bits != 32) {
        bits = 32;
    }
    g_engine.bit_depth = bits;
}
