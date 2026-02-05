#ifndef DSL_H
#define DSL_H

#include <stddef.h>

#define DSL_MAX_NAME 32
#define DSL_MAX_PATTERN 128
#define DSL_MAX_PATTERNS 64
#define DSL_MAX_SYNTHS 32
#define DSL_MAX_TRACKS 128
#define DSL_MAX_SEQUENCES 8
#define DSL_MAX_SEQUENCE_STEPS 32
#define DSL_MAX_DRONES 4

typedef enum {
    SYNTH_SINE,
    SYNTH_SAW,
    SYNTH_SUPERSAW,
    SYNTH_SQUARE,
    SYNTH_TRI,
    SYNTH_NOISE,
    SYNTH_PULSE,
    SYNTH_FM,
    SYNTH_RING,
    SYNTH_ACID,
    SYNTH_KICK,
    SYNTH_KICK808,
    SYNTH_KICK909,
    SYNTH_SNARE,
    SYNTH_SNARE808,
    SYNTH_SNARE909,
    SYNTH_CLAP,
    SYNTH_CLAP909,
    SYNTH_HAT_C,
    SYNTH_HAT_O,
    SYNTH_HAT808,
    SYNTH_HAT909,
    SYNTH_TOM,
    SYNTH_RIM,
    SYNTH_GLITCH,
    SYNTH_METAL,
    SYNTH_BITPERC,
    SYNTH_FM2,
    SYNTH_COMB,
    SYNTH_PM_STRING,
    SYNTH_PM_BELL,
    SYNTH_PM_PIPE,
    SYNTH_PM_KICK,
    SYNTH_PM_SNARE,
    SYNTH_PM_HAT,
    SYNTH_PM_CLAP,
    SYNTH_PM_TOM
} SynthType;

typedef enum {
    MOD_SRC_LFO,
    MOD_SRC_ENV,
    MOD_SRC_NOISE,
    MOD_SRC_SAMPLE_HOLD,
    MOD_SRC_RING,
    MOD_SRC_SYNC
} ModSource;

typedef enum {
    MOD_DEST_AMP,
    MOD_DEST_CUTOFF,
    MOD_DEST_RES,
    MOD_DEST_PAN,
    MOD_DEST_PITCH
} ModDest;

typedef struct ModDef {
    ModSource source;
    ModDest dest;
    float rate;
    float depth;
    float offset;
    float lag_ms;
    float slew_ms;
} ModDef;

typedef struct {
    char name[DSL_MAX_NAME];
    SynthType type;
    float amp;
    float cutoff;
    float res;
    float atk;
    float dec;
    float sus;
    float rel;
    float comb_feedback;
    float comb_damp;
    float comb_excite;
    float detune_rate;
    float detune_depth;
    float drive;
    int mod_count;
    ModDef mods[32];
} SynthDef;

typedef struct {
    char name[DSL_MAX_NAME];
    int length;
    int notes[DSL_MAX_PATTERN]; // MIDI note numbers, -1 for rest
    float cents[DSL_MAX_PATTERN]; // microtonal offset in cents
    int degree[DSL_MAX_PATTERN];
    int degree_octave[DSL_MAX_PATTERN];
    int degree_micro[DSL_MAX_PATTERN]; // -1, 0, +1 quarter-tone
    int degree_valid[DSL_MAX_PATTERN];
    float slide_ms[DSL_MAX_PATTERN]; // per-note override, <0 means use track
    int accent[DSL_MAX_PATTERN]; // 303 accent
} PatternDef;

typedef struct {
    char pattern[DSL_MAX_NAME];
    int repeat;
} SequenceStep;

typedef struct {
    char name[DSL_MAX_NAME];
    int count;
    SequenceStep steps[DSL_MAX_SEQUENCE_STEPS];
} SequenceDef;

typedef struct {
    char synth[DSL_MAX_NAME];
    float midi;
} DroneDef;

typedef struct {
    char pattern[DSL_MAX_NAME];
    char synth[DSL_MAX_NAME];
    int is_sequence;
    int seq_start;
    int seq_end;
    float rate;
    float hurry;
    int fast;
    int slow;
    int every;
    float density;
    int rev;
    int rev_transpose;
    int palindrome;
    int offset_bars;
    int iter;
    int chunk;
    int stut;
    float slide_ms;
    float ornament_prob;
    int ornament_mode; // 0=down,1=up,2=alt
    float accent_prob;
} TrackDef;

typedef struct {
    float tempo;
    float master_amp;
    float root_midi;
    float maqam_offsets[7];
    float tempo_scale;
    float tempo_map[16]; // section index 1..14
    int time_sig_num;
    int time_sig_den;
    int time_sig_num_map[16]; // section index 1..14
    int time_sig_den_map[16];
    int time_sig_enforce;
    int time_sig_seq_len;
    int time_sig_seq_num[1024];
    int time_sig_seq_den[1024];

    int synth_count;
    SynthDef synths[DSL_MAX_SYNTHS];

    int pattern_count;
    PatternDef patterns[DSL_MAX_PATTERNS];

    int sequence_count;
    SequenceDef sequences[DSL_MAX_SEQUENCES];

    int drone_count;
    DroneDef drones[DSL_MAX_DRONES];

    int track_count;
    TrackDef tracks[DSL_MAX_TRACKS];
} Program;

int dsl_parse_script(const char *script, Program *out_program, char *error, size_t error_len);

int dsl_find_synth(const Program *program, const char *name);
int dsl_find_pattern(const Program *program, const char *name);
int dsl_find_sequence(const Program *program, const char *name);

#endif
