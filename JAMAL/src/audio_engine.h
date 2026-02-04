#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_engine_init(void);
void audio_engine_shutdown(void);

// Loads and starts playback from a DSL script. Returns 0 on success.
int audio_engine_play_script(const char *script, char *error, size_t error_len);

void audio_engine_stop(void);

void audio_engine_get_meter(float *out_left, float *out_right);
void audio_engine_get_meter_ex(float *out_rms_l, float *out_rms_r, float *out_peak_l, float *out_peak_r, int *out_clip);
int audio_engine_is_running(void);
float audio_engine_get_tempo(void);
unsigned long long audio_engine_get_pattern_epoch(void);
void audio_engine_set_master(float amp);
void audio_engine_set_output_device(unsigned int device_id);
void audio_engine_set_sample_rate(double sample_rate);
void audio_engine_set_buffer_frames(int frames);
void audio_engine_set_bit_depth(int bits);
int audio_engine_render_to_wav(const char *script, const char *path, double seconds, int sample_rate, int buffer_frames, char *error, size_t error_len);

#ifdef __cplusplus
}
#endif

#endif
