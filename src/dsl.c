#include "dsl.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_default_program(Program *program) {
    memset(program, 0, sizeof(*program));
    program->tempo = 120.0f;
    program->master_amp = 0.8f;
    program->root_midi = 60.0f;
    program->tempo_scale = 2.0f;
    for (int i = 0; i < 16; i++) program->tempo_map[i] = 1.0f;
    program->time_sig_num = 4;
    program->time_sig_den = 4;
    for (int i = 0; i < 16; i++) {
        program->time_sig_num_map[i] = 4;
        program->time_sig_den_map[i] = 4;
    }
    program->time_sig_enforce = 0;
    float neutral[7] = {0, 200, 400, 500, 700, 900, 1100};
    memcpy(program->maqam_offsets, neutral, sizeof(neutral));
}

static int parse_time_sig(const char *token, int *out_num, int *out_den) {
    if (!token || !*token) {
        return 0;
    }
    const char *slash = strchr(token, '/');
    if (!slash) {
        return 0;
    }
    char num_buf[16] = {0};
    char den_buf[16] = {0};
    size_t nlen = (size_t)(slash - token);
    if (nlen == 0 || nlen >= sizeof(num_buf)) {
        return 0;
    }
    memcpy(num_buf, token, nlen);
    strncpy(den_buf, slash + 1, sizeof(den_buf) - 1);
    int num = atoi(num_buf);
    int den = atoi(den_buf);
    if (num < 1 || num > 32) {
        return 0;
    }
    if (!(den == 1 || den == 2 || den == 4 || den == 8 || den == 16 || den == 32)) {
        return 0;
    }
    *out_num = num;
    *out_den = den;
    return 1;
}

static int pad_pattern_to_timesig(const Program *program, PatternDef *pattern, char *error, size_t error_len) {
    if (!program->time_sig_enforce) {
        return 1;
    }
    int num = program->time_sig_num;
    int den = program->time_sig_den;
    if (num <= 0 || den <= 0) {
        return 1;
    }
    if (16 % den != 0) {
        snprintf(error, error_len, "timesig_enforce only supports denominators 1,2,4,8,16");
        return 0;
    }
    int steps_per_beat = 16 / den;
    int bar_steps = num * steps_per_beat;
    if (bar_steps <= 0) {
        return 1;
    }
    int rem = pattern->length % bar_steps;
    if (rem == 0) {
        return 1;
    }
    int pad = bar_steps - rem;
    if (pattern->length + pad > DSL_MAX_PATTERN) {
        snprintf(error, error_len, "Pattern too long after timesig pad (max %d)", DSL_MAX_PATTERN);
        return 0;
    }
    for (int i = 0; i < pad; i++) {
        pattern->notes[pattern->length] = -1;
        pattern->cents[pattern->length] = 0.0f;
        pattern->degree_valid[pattern->length] = 0;
        pattern->degree[pattern->length] = 0;
        pattern->degree_octave[pattern->length] = 0;
        pattern->degree_micro[pattern->length] = 0;
        pattern->slide_ms[pattern->length] = -1.0f;
        pattern->accent[pattern->length] = 0;
        pattern->length++;
    }
    return 1;
}

static void trim_left(char **p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static int next_token(char **p, char *out, size_t out_len, int allow_quoted) {
    trim_left(p);
    if (**p == '\0') {
        return 0;
    }

    if (allow_quoted && (**p == '"' || **p == '(')) {
        char end = (**p == '(') ? ')' : '"';
        (*p)++;
        size_t i = 0;
        while (**p && **p != end) {
            if (i + 1 < out_len) {
                out[i++] = **p;
            }
            (*p)++;
        }
        if (**p == end) {
            (*p)++;
        }
        out[i] = '\0';
        return 1;
    }

    size_t i = 0;
    while (**p && !isspace((unsigned char)**p)) {
        if (i + 1 < out_len) {
            out[i++] = **p;
        }
        (*p)++;
    }
    out[i] = '\0';
    return 1;
}

static int parse_synth_type(const char *token, SynthType *out_type) {
    if (strcmp(token, "sine") == 0) {
        *out_type = SYNTH_SINE;
        return 1;
    }
    if (strcmp(token, "saw") == 0) {
        *out_type = SYNTH_SAW;
        return 1;
    }
    if (strcmp(token, "supersaw") == 0) {
        *out_type = SYNTH_SUPERSAW;
        return 1;
    }
    if (strcmp(token, "square") == 0) {
        *out_type = SYNTH_SQUARE;
        return 1;
    }
    if (strcmp(token, "tri") == 0 || strcmp(token, "triangle") == 0) {
        *out_type = SYNTH_TRI;
        return 1;
    }
    if (strcmp(token, "noise") == 0) {
        *out_type = SYNTH_NOISE;
        return 1;
    }
    if (strcmp(token, "pulse") == 0) {
        *out_type = SYNTH_PULSE;
        return 1;
    }
    if (strcmp(token, "fm") == 0) {
        *out_type = SYNTH_FM;
        return 1;
    }
    if (strcmp(token, "ring") == 0) {
        *out_type = SYNTH_RING;
        return 1;
    }
    if (strcmp(token, "acid") == 0) {
        *out_type = SYNTH_ACID;
        return 1;
    }
    if (strcmp(token, "kick") == 0) {
        *out_type = SYNTH_KICK;
        return 1;
    }
    if (strcmp(token, "kick808") == 0) {
        *out_type = SYNTH_KICK808;
        return 1;
    }
    if (strcmp(token, "kick909") == 0) {
        *out_type = SYNTH_KICK909;
        return 1;
    }
    if (strcmp(token, "snare") == 0) {
        *out_type = SYNTH_SNARE;
        return 1;
    }
    if (strcmp(token, "snare808") == 0) {
        *out_type = SYNTH_SNARE808;
        return 1;
    }
    if (strcmp(token, "snare909") == 0) {
        *out_type = SYNTH_SNARE909;
        return 1;
    }
    if (strcmp(token, "clap") == 0) {
        *out_type = SYNTH_CLAP;
        return 1;
    }
    if (strcmp(token, "clap909") == 0) {
        *out_type = SYNTH_CLAP909;
        return 1;
    }
    if (strcmp(token, "hatc") == 0 || strcmp(token, "hat_c") == 0 || strcmp(token, "hat-closed") == 0) {
        *out_type = SYNTH_HAT_C;
        return 1;
    }
    if (strcmp(token, "hato") == 0 || strcmp(token, "hat_o") == 0 || strcmp(token, "hat-open") == 0) {
        *out_type = SYNTH_HAT_O;
        return 1;
    }
    if (strcmp(token, "hat808") == 0) {
        *out_type = SYNTH_HAT808;
        return 1;
    }
    if (strcmp(token, "hat909") == 0) {
        *out_type = SYNTH_HAT909;
        return 1;
    }
    if (strcmp(token, "tom") == 0) {
        *out_type = SYNTH_TOM;
        return 1;
    }
    if (strcmp(token, "rim") == 0 || strcmp(token, "rimshot") == 0) {
        *out_type = SYNTH_RIM;
        return 1;
    }
    if (strcmp(token, "glitch") == 0) {
        *out_type = SYNTH_GLITCH;
        return 1;
    }
    if (strcmp(token, "metal") == 0) {
        *out_type = SYNTH_METAL;
        return 1;
    }
    if (strcmp(token, "bitperc") == 0 || strcmp(token, "bit") == 0) {
        *out_type = SYNTH_BITPERC;
        return 1;
    }
    if (strcmp(token, "fm2") == 0) {
        *out_type = SYNTH_FM2;
        return 1;
    }
    if (strcmp(token, "comb") == 0 || strcmp(token, "res") == 0 || strcmp(token, "resonator") == 0) {
        *out_type = SYNTH_COMB;
        return 1;
    }
    if (strcmp(token, "pm_string") == 0 || strcmp(token, "pmstring") == 0) {
        *out_type = SYNTH_PM_STRING;
        return 1;
    }
    if (strcmp(token, "pm_bell") == 0 || strcmp(token, "pmbell") == 0) {
        *out_type = SYNTH_PM_BELL;
        return 1;
    }
    if (strcmp(token, "pm_pipe") == 0 || strcmp(token, "pmpipe") == 0) {
        *out_type = SYNTH_PM_PIPE;
        return 1;
    }
    if (strcmp(token, "pm_kick") == 0 || strcmp(token, "pmkick") == 0) {
        *out_type = SYNTH_PM_KICK;
        return 1;
    }
    if (strcmp(token, "pm_snare") == 0 || strcmp(token, "pmsnare") == 0) {
        *out_type = SYNTH_PM_SNARE;
        return 1;
    }
    if (strcmp(token, "pm_hat") == 0 || strcmp(token, "pmhat") == 0) {
        *out_type = SYNTH_PM_HAT;
        return 1;
    }
    if (strcmp(token, "pm_clap") == 0 || strcmp(token, "pmclap") == 0) {
        *out_type = SYNTH_PM_CLAP;
        return 1;
    }
    if (strcmp(token, "pm_tom") == 0 || strcmp(token, "pmtom") == 0) {
        *out_type = SYNTH_PM_TOM;
        return 1;
    }
    return 0;
}

static int note_name_to_midi(const char *token) {
    if (token[0] == '\0') {
        return -1;
    }

    if (isdigit((unsigned char)token[0]) || (token[0] == '-' && isdigit((unsigned char)token[1]))) {
        return atoi(token);
    }

    char note = (char)toupper((unsigned char)token[0]);
    int semitone = 0;
    switch (note) {
        case 'C': semitone = 0; break;
        case 'D': semitone = 2; break;
        case 'E': semitone = 4; break;
        case 'F': semitone = 5; break;
        case 'G': semitone = 7; break;
        case 'A': semitone = 9; break;
        case 'B': semitone = 11; break;
        default: return -2;
    }

    int index = 1;
    if (token[index] == '#') {
        semitone += 1;
        index++;
    } else if (token[index] == 'b' || token[index] == 'B') {
        semitone -= 1;
        index++;
    }

    if (!isdigit((unsigned char)token[index]) && token[index] != '-') {
        return -2;
    }

    int octave = atoi(&token[index]);
    int midi = (octave + 1) * 12 + semitone;
    return midi;
}

static void set_maqam(Program *program, const char *name) {
    if (strcmp(name, "rast") == 0) {
        float offsets[7] = {0, 200, 350, 500, 700, 900, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "bayati") == 0) {
        float offsets[7] = {0, 150, 300, 500, 700, 850, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "hijaz") == 0) {
        float offsets[7] = {0, 100, 400, 500, 700, 800, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "nahawand") == 0) {
        float offsets[7] = {0, 200, 300, 500, 700, 800, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "saba") == 0) {
        float offsets[7] = {0, 150, 300, 400, 700, 900, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "kurd") == 0) {
        float offsets[7] = {0, 100, 300, 500, 700, 800, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "lydian") == 0) {
        float offsets[7] = {0, 200, 400, 600, 700, 900, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "major") == 0 || strcmp(name, "ionian") == 0) {
        float offsets[7] = {0, 200, 400, 500, 700, 900, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "minor") == 0 || strcmp(name, "aeolian") == 0) {
        float offsets[7] = {0, 200, 300, 500, 700, 800, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "dorian") == 0) {
        float offsets[7] = {0, 200, 300, 500, 700, 900, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "phrygian") == 0) {
        float offsets[7] = {0, 100, 300, 500, 700, 800, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "mixolydian") == 0) {
        float offsets[7] = {0, 200, 400, 500, 700, 900, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "locrian") == 0) {
        float offsets[7] = {0, 100, 300, 500, 600, 800, 1000};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "harmonic_minor") == 0 || strcmp(name, "harmonic-minor") == 0) {
        float offsets[7] = {0, 200, 300, 500, 700, 800, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "melodic_minor") == 0 || strcmp(name, "melodic-minor") == 0) {
        float offsets[7] = {0, 200, 300, 500, 700, 900, 1100};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "pentatonic_major") == 0 || strcmp(name, "pentatonic-major") == 0 || strcmp(name, "pentatonic") == 0) {
        float offsets[7] = {0, 200, 400, 700, 900, 1200, 1400};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "pentatonic_minor") == 0 || strcmp(name, "pentatonic-minor") == 0) {
        float offsets[7] = {0, 300, 500, 700, 1000, 1200, 1400};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "blues") == 0 || strcmp(name, "blues_minor") == 0 || strcmp(name, "blues-minor") == 0) {
        float offsets[7] = {0, 300, 500, 600, 700, 1000, 1200};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "blues_major") == 0 || strcmp(name, "blues-major") == 0) {
        float offsets[7] = {0, 200, 300, 400, 700, 900, 1200};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "whole_tone") == 0 || strcmp(name, "whole-tone") == 0) {
        float offsets[7] = {0, 200, 400, 600, 800, 1000, 1200};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "octatonic") == 0 || strcmp(name, "octatonic_wh") == 0 || strcmp(name, "octatonic-wh") == 0) {
        float offsets[7] = {0, 200, 300, 500, 600, 800, 900};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    } else if (strcmp(name, "octatonic_hw") == 0 || strcmp(name, "octatonic-hw") == 0) {
        float offsets[7] = {0, 100, 300, 400, 600, 700, 900};
        memcpy(program->maqam_offsets, offsets, sizeof(offsets));
    }
}

static int parse_degree_token_info(const char *token,
                                   float root_midi,
                                   const float *maqam_offsets,
                                   float *out_midi,
                                   int *out_degree,
                                   int *out_octave,
                                   int *out_micro) {
    if (!token || !token[0]) {
        return 0;
    }
    if (strcmp(token, ".") == 0 || strcmp(token, "-") == 0) {
        *out_midi = -1.0f;
        if (out_degree) *out_degree = 0;
        if (out_octave) *out_octave = 0;
        if (out_micro) *out_micro = 0;
        return 1;
    }
    if (token[0] == 'r' || token[0] == 'R') {
        *out_midi = root_midi;
        if (out_degree) *out_degree = 1;
        if (out_octave) *out_octave = 0;
        if (out_micro) *out_micro = 0;
        return 1;
    }

    int degree = token[0] - '0';
    if (degree < 1 || degree > 7) {
        return 0;
    }
    int index = 1;
    int octave_offset = 0;
    while (token[index] == '\'') {
        octave_offset += 12;
        index++;
    }
    while (token[index] == ',') {
        octave_offset -= 12;
        index++;
    }

    int micro = 0;
    float cents = maqam_offsets[degree - 1];
    if (token[index] == '+') {
        cents += 50.0f;
        micro = 1;
        index++;
    } else if (token[index] == '-') {
        cents -= 50.0f;
        micro = -1;
        index++;
    }

    if (token[index] != '\0') {
        return 0;
    }

    float midi = root_midi + octave_offset + (cents / 100.0f);
    *out_midi = midi;
    if (out_degree) *out_degree = degree;
    if (out_octave) *out_octave = octave_offset / 12;
    if (out_micro) *out_micro = micro;
    return 1;
}

static int parse_degree_token(const char *token, float root_midi, const float *maqam_offsets, float *out_midi) {
    return parse_degree_token_info(token, root_midi, maqam_offsets, out_midi, NULL, NULL, NULL);
}

static int split_token_slide(const char *token, char *base, size_t base_len, float *slide_ms, int *accent) {
    const char *excl = strchr(token, '!');
    const char *tilde = strchr(token, '~');
    *accent = 0;
    size_t len = strlen(token);
    if (excl) {
        *accent = 1;
        len = (size_t)(excl - token);
    }
    if (tilde && (!excl || tilde < excl)) {
        len = (size_t)(tilde - token);
    }
    if (len >= base_len) len = base_len - 1;
    memcpy(base, token, len);
    base[len] = '\0';
    if (!tilde) {
        *slide_ms = -1.0f;
        return 1;
    }
    if (!tilde) {
        *slide_ms = -1.0f;
        return 1;
    }
    *slide_ms = (float)atof(tilde + 1);
    if (*slide_ms < 0.0f) *slide_ms = 0.0f;
    return 1;
}

static int parse_pattern_list(const char *sequence, PatternDef *pattern, char *error, size_t error_len) {
    pattern->length = 0;

    const char *open = strchr(sequence, '[');
    const char *close = open ? strchr(open, ']') : NULL;
    if (!open || !close || close <= open) {
        snprintf(error, error_len, "Pattern list must be like [60, 62, 64]");
        return 0;
    }

    char list_buf[512];
    size_t list_len = (size_t)(close - open - 1);
    if (list_len >= sizeof(list_buf)) list_len = sizeof(list_buf) - 1;
    memcpy(list_buf, open + 1, list_len);
    list_buf[list_len] = '\0';

    int base_notes[DSL_MAX_PATTERN];
    float base_cents[DSL_MAX_PATTERN];
    int base_degree[DSL_MAX_PATTERN];
    int base_degree_octave[DSL_MAX_PATTERN];
    int base_degree_micro[DSL_MAX_PATTERN];
    int base_degree_valid[DSL_MAX_PATTERN];
    float base_slide[DSL_MAX_PATTERN];
    int base_accent[DSL_MAX_PATTERN];
    int base_len = 0;
    memset(base_degree, 0, sizeof(base_degree));
    memset(base_degree_octave, 0, sizeof(base_degree_octave));
    memset(base_degree_micro, 0, sizeof(base_degree_micro));
    memset(base_degree_valid, 0, sizeof(base_degree_valid));
    for (int i = 0; i < DSL_MAX_PATTERN; i++) {
        base_slide[i] = -1.0f;
        base_accent[i] = 0;
    }

    char *cursor = list_buf;
    while (*cursor) {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) {
            cursor++;
        }
        if (!*cursor) break;

        char token[32] = {0};
        size_t i = 0;
        while (*cursor && !isspace((unsigned char)*cursor) && *cursor != ',') {
            if (i + 1 < sizeof(token)) {
                token[i++] = *cursor;
            }
            cursor++;
        }
        token[i] = '\0';

        if (base_len >= DSL_MAX_PATTERN) {
            snprintf(error, error_len, "Pattern too long (max %d)", DSL_MAX_PATTERN);
            return 0;
        }

        char base[32] = {0};
        float slide = -1.0f;
        int accent = 0;
        split_token_slide(token, base, sizeof(base), &slide, &accent);

        if (strcmp(base, ".") == 0 || strcmp(base, "-") == 0) {
            base_notes[base_len++] = -1;
            base_cents[base_len - 1] = 0.0f;
            base_degree_valid[base_len - 1] = 0;
            base_slide[base_len - 1] = slide;
            base_accent[base_len - 1] = accent;
            continue;
        }

        int midi = note_name_to_midi(base);
        if (midi == -2) {
            snprintf(error, error_len, "Invalid note token '%s'", base);
            return 0;
        }
        base_notes[base_len++] = midi;
        base_cents[base_len - 1] = 0.0f;
        base_degree_valid[base_len - 1] = 0;
        base_slide[base_len - 1] = slide;
        base_accent[base_len - 1] = accent;
    }

    if (base_len == 0) {
        snprintf(error, error_len, "Pattern must have at least one step");
        return 0;
    }

    int repeat = 1;
    const char *after = close + 1;
    while (*after && (isspace((unsigned char)*after) || *after == ',' || *after == ')')) {
        after++;
    }
    if (*after) {
        char rep[16] = {0};
        size_t i = 0;
        while (*after && !isspace((unsigned char)*after) && *after != ')') {
            if (i + 1 < sizeof(rep)) rep[i++] = *after;
            after++;
        }
        rep[i] = '\0';
        if (strcmp(rep, "inf") == 0) {
            repeat = 1;
        } else {
            repeat = atoi(rep);
            if (repeat < 1) {
                snprintf(error, error_len, "Repeat must be >= 1 or 'inf'");
                return 0;
            }
        }
    }

    for (int r = 0; r < repeat; r++) {
        for (int i = 0; i < base_len; i++) {
            if (pattern->length >= DSL_MAX_PATTERN) {
                snprintf(error, error_len, "Pattern too long (max %d)", DSL_MAX_PATTERN);
                return 0;
            }
            pattern->notes[pattern->length] = base_notes[i];
            pattern->cents[pattern->length] = base_cents[i];
            pattern->degree_valid[pattern->length] = base_degree_valid[i];
            pattern->degree[pattern->length] = base_degree[i];
            pattern->degree_octave[pattern->length] = base_degree_octave[i];
            pattern->degree_micro[pattern->length] = base_degree_micro[i];
            pattern->slide_ms[pattern->length] = base_slide[i];
            pattern->accent[pattern->length] = base_accent[i];
            pattern->length++;
        }
    }

    return 1;
}

static int parse_pattern(const char *sequence, PatternDef *pattern, char *error, size_t error_len, const Program *program) {
    pattern->length = 0;

    if (sequence[0] == '[' || strstr(sequence, "[") != NULL) {
        return parse_pattern_list(sequence, pattern, error, error_len);
    }

    char copy[512];
    strncpy(copy, sequence, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *cursor = copy;
    while (*cursor) {
        while (*cursor && (isspace((unsigned char)*cursor) || *cursor == ',')) {
            cursor++;
        }
        if (!*cursor) {
            break;
        }

        char token[32] = {0};
        size_t i = 0;
        while (*cursor && !isspace((unsigned char)*cursor) && *cursor != ',') {
            if (i + 1 < sizeof(token)) {
                token[i++] = *cursor;
            }
            cursor++;
        }
        token[i] = '\0';

        if (pattern->length >= DSL_MAX_PATTERN) {
            snprintf(error, error_len, "Pattern too long (max %d)", DSL_MAX_PATTERN);
            return 0;
        }

        char base[32] = {0};
        float slide = -1.0f;
        int accent = 0;
        split_token_slide(token, base, sizeof(base), &slide, &accent);

        if (strcmp(base, ".") == 0 || strcmp(base, "-") == 0) {
            pattern->notes[pattern->length] = -1;
            pattern->cents[pattern->length] = 0.0f;
            pattern->degree_valid[pattern->length] = 0;
            pattern->slide_ms[pattern->length] = slide;
            pattern->accent[pattern->length] = accent;
            pattern->length++;
            continue;
        }

        float midi_f = 0.0f;
        int deg = 0;
        int oct = 0;
        int micro = 0;
        if (parse_degree_token_info(base, program->root_midi, program->maqam_offsets, &midi_f, &deg, &oct, &micro)) {
            pattern->notes[pattern->length] = (int)floorf(midi_f);
            pattern->cents[pattern->length] = (midi_f - floorf(midi_f)) * 100.0f;
            pattern->degree_valid[pattern->length] = 1;
            pattern->degree[pattern->length] = deg;
            pattern->degree_octave[pattern->length] = oct;
            pattern->degree_micro[pattern->length] = micro;
            pattern->slide_ms[pattern->length] = slide;
            pattern->accent[pattern->length] = accent;
            pattern->length++;
            continue;
        }

        int midi = note_name_to_midi(base);
        if (midi == -2) {
            snprintf(error, error_len, "Invalid note token '%s'", base);
            return 0;
        }
        pattern->notes[pattern->length] = midi;
        pattern->cents[pattern->length] = 0.0f;
        pattern->degree_valid[pattern->length] = 0;
        pattern->slide_ms[pattern->length] = slide;
        pattern->accent[pattern->length] = accent;
        pattern->length++;
    }

    if (pattern->length == 0) {
        snprintf(error, error_len, "Pattern must have at least one step");
        return 0;
    }

    return 1;
}

int dsl_find_synth(const Program *program, const char *name) {
    for (int i = 0; i < program->synth_count; i++) {
        if (strcmp(program->synths[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int dsl_find_pattern(const Program *program, const char *name) {
    for (int i = 0; i < program->pattern_count; i++) {
        if (strcmp(program->patterns[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

int dsl_find_sequence(const Program *program, const char *name) {
    for (int i = 0; i < program->sequence_count; i++) {
        if (strcmp(program->sequences[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void set_default_synth(SynthDef *synth) {
    synth->amp = 0.5f;
    synth->cutoff = 18000.0f;
    synth->res = 0.1f;
    synth->atk = 0.01f;
    synth->dec = 0.1f;
    synth->sus = 0.6f;
    synth->rel = 0.2f;
    synth->comb_feedback = 0.85f;
    synth->comb_damp = 0.2f;
    synth->comb_excite = 0.7f;
    synth->mod_count = 0;
}

static void set_default_track(TrackDef *track) {
    track->is_sequence = 0;
    track->seq_start = 0;
    track->seq_end = -1;
    track->rate = 1.0f;
    track->hurry = 1.0f;
    track->fast = 1;
    track->slow = 1;
    track->every = 1;
    track->density = 1.0f;
    track->rev = 0;
    track->palindrome = 0;
    track->iter = 1;
    track->chunk = 0;
    track->stut = 1;
    track->slide_ms = 0.0f;
    track->ornament_prob = 0.0f;
    track->ornament_mode = 0;
    track->accent_prob = 0.0f;
}

int dsl_parse_script(const char *script, Program *out_program, char *error, size_t error_len) {
    set_default_program(out_program);

    char *script_copy = strdup(script ? script : "");
    if (!script_copy) {
        snprintf(error, error_len, "Out of memory");
        return 0;
    }

    char *saveptr = NULL;
    int line_num = 0;
    for (char *line = strtok_r(script_copy, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        line_num++;
        char *cursor = line;

        char *comment = strstr(cursor, "//");
        if (comment) {
            *comment = '\0';
        }
        comment = strchr(cursor, '#');
        if (comment) {
            *comment = '\0';
        }

        trim_left(&cursor);
        if (*cursor == '\0') {
            continue;
        }

        char cmd[32] = {0};
        if (!next_token(&cursor, cmd, sizeof(cmd), 0)) {
            continue;
        }

        if (strcmp(cmd, "tempo") == 0) {
            char bpm_token[32] = {0};
            if (!next_token(&cursor, bpm_token, sizeof(bpm_token), 0)) {
                snprintf(error, error_len, "Line %d: tempo requires a value", line_num);
                free(script_copy);
                return 0;
            }
            float bpm = (float)atof(bpm_token);
            if (bpm < 20.0f || bpm > 300.0f) {
                snprintf(error, error_len, "Line %d: tempo out of range", line_num);
                free(script_copy);
                return 0;
            }
            out_program->tempo = bpm * out_program->tempo_scale;
            continue;
        }

        if (strcmp(cmd, "master") == 0 || strcmp(cmd, "master_amp") == 0) {
            char amp_token[32] = {0};
            if (!next_token(&cursor, amp_token, sizeof(amp_token), 0)) {
                snprintf(error, error_len, "Line %d: master requires a value", line_num);
                free(script_copy);
                return 0;
            }
            float amp = (float)atof(amp_token);
            if (amp < 0.0f || amp > 4.0f) {
                snprintf(error, error_len, "Line %d: master out of range", line_num);
                free(script_copy);
                return 0;
            }
            out_program->master_amp = amp;
            continue;
        }

        if (strcmp(cmd, "tempo_scale") == 0) {
            char scale_token[32] = {0};
            if (!next_token(&cursor, scale_token, sizeof(scale_token), 0)) {
                snprintf(error, error_len, "Line %d: tempo_scale requires a value", line_num);
                free(script_copy);
                return 0;
            }
            float scale = (float)atof(scale_token);
            if (scale <= 0.0f || scale > 8.0f) {
                snprintf(error, error_len, "Line %d: tempo_scale out of range", line_num);
                free(script_copy);
                return 0;
            }
            out_program->tempo_scale = scale;
            continue;
        }

        if (strcmp(cmd, "tempo_map") == 0) {
            char map[256] = {0};
            if (!next_token(&cursor, map, sizeof(map), 1)) {
                snprintf(error, error_len, "Line %d: tempo_map requires values", line_num);
                free(script_copy);
                return 0;
            }
            char copy[256];
            strncpy(copy, map, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            char *c = copy;
            while (*c) {
                while (*c && (isspace((unsigned char)*c) || *c == ',')) c++;
                if (!*c) break;
                char token[64] = {0};
                size_t i = 0;
                while (*c && !isspace((unsigned char)*c) && *c != ',') {
                    if (i + 1 < sizeof(token)) token[i++] = *c;
                    c++;
                }
                token[i] = '\0';
                char *eq = strchr(token, '=');
                if (!eq) {
                    snprintf(error, error_len, "Line %d: tempo_map expects key=value", line_num);
                    free(script_copy);
                    return 0;
                }
                *eq = '\0';
                const char *key = token;
                float val = (float)atof(eq + 1);
                if (val <= 0.0f || val > 4.0f) {
                    snprintf(error, error_len, "Line %d: tempo_map value out of range", line_num);
                    free(script_copy);
                    return 0;
                }
                if (strcmp(key, "intro") == 0) {
                    out_program->tempo_map[1] = val;
                } else if (strcmp(key, "verse") == 0) {
                    out_program->tempo_map[2] = val;
                    out_program->tempo_map[4] = val;
                } else if (strcmp(key, "chorus") == 0) {
                    out_program->tempo_map[3] = val;
                    out_program->tempo_map[5] = val;
                } else if (strcmp(key, "bridge") == 0) {
                    out_program->tempo_map[6] = val;
                } else if (strcmp(key, "final") == 0) {
                    out_program->tempo_map[7] = val;
                } else if (isdigit((unsigned char)key[0])) {
                    int idx = atoi(key);
                    if (idx < 1 || idx > 14) {
                        snprintf(error, error_len, "Line %d: tempo_map index must be 1-14", line_num);
                        free(script_copy);
                        return 0;
                    }
                    out_program->tempo_map[idx] = val;
                } else {
                    snprintf(error, error_len, "Line %d: unknown tempo_map key '%s'", line_num, key);
                    free(script_copy);
                    return 0;
                }
            }
            continue;
        }

        if (strcmp(cmd, "timesig") == 0 || strcmp(cmd, "time_signature") == 0) {
            char sig_token[32] = {0};
            if (!next_token(&cursor, sig_token, sizeof(sig_token), 0)) {
                snprintf(error, error_len, "Line %d: timesig requires a value", line_num);
                free(script_copy);
                return 0;
            }
            int num = 0, den = 0;
            if (strchr(sig_token, '/')) {
                if (!parse_time_sig(sig_token, &num, &den)) {
                    snprintf(error, error_len, "Line %d: invalid timesig '%s'", line_num, sig_token);
                    free(script_copy);
                    return 0;
                }
            } else {
                char den_token[16] = {0};
                if (!next_token(&cursor, den_token, sizeof(den_token), 0)) {
                    snprintf(error, error_len, "Line %d: timesig requires numerator/denominator", line_num);
                    free(script_copy);
                    return 0;
                }
                char combined[32] = {0};
                snprintf(combined, sizeof(combined), "%s/%s", sig_token, den_token);
                if (!parse_time_sig(combined, &num, &den)) {
                    snprintf(error, error_len, "Line %d: invalid timesig '%s/%s'", line_num, sig_token, den_token);
                    free(script_copy);
                    return 0;
                }
            }
            out_program->time_sig_num = num;
            out_program->time_sig_den = den;
            for (int i = 1; i < 16; i++) {
                out_program->time_sig_num_map[i] = num;
                out_program->time_sig_den_map[i] = den;
            }
            continue;
        }

        if (strcmp(cmd, "timesig_enforce") == 0) {
            char flag[16] = {0};
            if (!next_token(&cursor, flag, sizeof(flag), 0)) {
                snprintf(error, error_len, "Line %d: timesig_enforce requires on/off", line_num);
                free(script_copy);
                return 0;
            }
            if (strcmp(flag, "on") == 0 || strcmp(flag, "true") == 0 || strcmp(flag, "1") == 0) {
                out_program->time_sig_enforce = 1;
            } else if (strcmp(flag, "off") == 0 || strcmp(flag, "false") == 0 || strcmp(flag, "0") == 0) {
                out_program->time_sig_enforce = 0;
            } else {
                snprintf(error, error_len, "Line %d: timesig_enforce expects on/off", line_num);
                free(script_copy);
                return 0;
            }
            continue;
        }

        if (strcmp(cmd, "timesig_map") == 0) {
            char map[256] = {0};
            if (!next_token(&cursor, map, sizeof(map), 1)) {
                snprintf(error, error_len, "Line %d: timesig_map requires values", line_num);
                free(script_copy);
                return 0;
            }
            char copy[256];
            strncpy(copy, map, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            char *c = copy;
            while (*c) {
                while (*c && (isspace((unsigned char)*c) || *c == ',')) c++;
                if (!*c) break;
                char token[64] = {0};
                size_t i = 0;
                while (*c && !isspace((unsigned char)*c) && *c != ',') {
                    if (i + 1 < sizeof(token)) token[i++] = *c;
                    c++;
                }
                token[i] = '\0';
                char *eq = strchr(token, '=');
                if (!eq) {
                    snprintf(error, error_len, "Line %d: timesig_map expects key=value", line_num);
                    free(script_copy);
                    return 0;
                }
                *eq = '\0';
                const char *key = token;
                int num = 0, den = 0;
                if (!parse_time_sig(eq + 1, &num, &den)) {
                    snprintf(error, error_len, "Line %d: invalid timesig '%s'", line_num, eq + 1);
                    free(script_copy);
                    return 0;
                }
                if (strcmp(key, "intro") == 0) {
                    out_program->time_sig_num_map[1] = num;
                    out_program->time_sig_den_map[1] = den;
                } else if (strcmp(key, "verse") == 0) {
                    out_program->time_sig_num_map[2] = num;
                    out_program->time_sig_den_map[2] = den;
                    out_program->time_sig_num_map[4] = num;
                    out_program->time_sig_den_map[4] = den;
                } else if (strcmp(key, "chorus") == 0) {
                    out_program->time_sig_num_map[3] = num;
                    out_program->time_sig_den_map[3] = den;
                    out_program->time_sig_num_map[5] = num;
                    out_program->time_sig_den_map[5] = den;
                } else if (strcmp(key, "bridge") == 0) {
                    out_program->time_sig_num_map[6] = num;
                    out_program->time_sig_den_map[6] = den;
                } else if (strcmp(key, "final") == 0) {
                    out_program->time_sig_num_map[7] = num;
                    out_program->time_sig_den_map[7] = den;
                } else if (isdigit((unsigned char)key[0])) {
                    int idx = atoi(key);
                    if (idx < 1 || idx > 14) {
                        snprintf(error, error_len, "Line %d: timesig_map index must be 1-14", line_num);
                        free(script_copy);
                        return 0;
                    }
                    out_program->time_sig_num_map[idx] = num;
                    out_program->time_sig_den_map[idx] = den;
                } else {
                    snprintf(error, error_len, "Line %d: unknown timesig_map key '%s'", line_num, key);
                    free(script_copy);
                    return 0;
                }
            }
            continue;
        }

        if (strcmp(cmd, "root") == 0) {
            char root_token[32] = {0};
            if (!next_token(&cursor, root_token, sizeof(root_token), 0)) {
                snprintf(error, error_len, "Line %d: root requires a note", line_num);
                free(script_copy);
                return 0;
            }
            int midi = note_name_to_midi(root_token);
            if (midi == -2) {
                snprintf(error, error_len, "Line %d: invalid root '%s'", line_num, root_token);
                free(script_copy);
                return 0;
            }
            out_program->root_midi = (float)midi;
            continue;
        }

        if (strcmp(cmd, "maqam") == 0) {
            char name[32] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0)) {
                snprintf(error, error_len, "Line %d: maqam requires a name", line_num);
                free(script_copy);
                return 0;
            }
            set_maqam(out_program, name);
            continue;
        }

        if (strcmp(cmd, "drone") == 0) {
            if (out_program->drone_count >= DSL_MAX_DRONES) {
                snprintf(error, error_len, "Line %d: too many drones", line_num);
                free(script_copy);
                return 0;
            }
            char synth[DSL_MAX_NAME] = {0};
            char note[32] = {0};
            if (!next_token(&cursor, synth, sizeof(synth), 0) || !next_token(&cursor, note, sizeof(note), 0)) {
                snprintf(error, error_len, "Line %d: drone requires synth and note/degree", line_num);
                free(script_copy);
                return 0;
            }
            float midi_f = 0.0f;
            if (!parse_degree_token(note, out_program->root_midi, out_program->maqam_offsets, &midi_f)) {
                int midi = note_name_to_midi(note);
                if (midi == -2) {
                    snprintf(error, error_len, "Line %d: invalid drone note '%s'", line_num, note);
                    free(script_copy);
                    return 0;
                }
                midi_f = (float)midi;
            }
            DroneDef *drone = &out_program->drones[out_program->drone_count++];
            memset(drone, 0, sizeof(*drone));
            strncpy(drone->synth, synth, sizeof(drone->synth) - 1);
            drone->midi = midi_f;
            continue;
        }

        if (strcmp(cmd, "amp") == 0) {
            char amp_token[32] = {0};
            if (!next_token(&cursor, amp_token, sizeof(amp_token), 0)) {
                snprintf(error, error_len, "Line %d: amp requires a value", line_num);
                free(script_copy);
                return 0;
            }
            out_program->master_amp = (float)atof(amp_token);
            continue;
        }

        if (strcmp(cmd, "synth") == 0) {
            if (out_program->synth_count >= DSL_MAX_SYNTHS) {
                snprintf(error, error_len, "Line %d: too many synths", line_num);
                free(script_copy);
                return 0;
            }
            char name[DSL_MAX_NAME] = {0};
            char type_token[32] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0) || !next_token(&cursor, type_token, sizeof(type_token), 0)) {
                snprintf(error, error_len, "Line %d: synth requires name and type", line_num);
                free(script_copy);
                return 0;
            }
            SynthType type;
            if (!parse_synth_type(type_token, &type)) {
                snprintf(error, error_len, "Line %d: unknown synth type '%s'", line_num, type_token);
                free(script_copy);
                return 0;
            }
            SynthDef *synth = &out_program->synths[out_program->synth_count++];
            memset(synth, 0, sizeof(*synth));
            strncpy(synth->name, name, sizeof(synth->name) - 1);
            synth->type = type;
            set_default_synth(synth);
            continue;
        }

        if (strcmp(cmd, "set") == 0) {
            char name[DSL_MAX_NAME] = {0};
            char param[32] = {0};
            char value[32] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0) ||
                !next_token(&cursor, param, sizeof(param), 0) ||
                !next_token(&cursor, value, sizeof(value), 0)) {
                snprintf(error, error_len, "Line %d: set requires synth, param, value", line_num);
                free(script_copy);
                return 0;
            }
            int idx = dsl_find_synth(out_program, name);
            if (idx < 0) {
                snprintf(error, error_len, "Line %d: unknown synth '%s'", line_num, name);
                free(script_copy);
                return 0;
            }
            SynthDef *synth = &out_program->synths[idx];
            float v = (float)atof(value);
            if (strcmp(param, "amp") == 0) synth->amp = v;
            else if (strcmp(param, "cutoff") == 0) synth->cutoff = v;
            else if (strcmp(param, "res") == 0) synth->res = v;
            else if (strcmp(param, "atk") == 0) synth->atk = v;
            else if (strcmp(param, "dec") == 0) synth->dec = v;
            else if (strcmp(param, "sus") == 0) synth->sus = v;
            else if (strcmp(param, "rel") == 0) synth->rel = v;
            else if (strcmp(param, "feedback") == 0) synth->comb_feedback = v;
            else if (strcmp(param, "damp") == 0) synth->comb_damp = v;
            else if (strcmp(param, "excite") == 0) synth->comb_excite = v;
            else {
                snprintf(error, error_len, "Line %d: unknown param '%s'", line_num, param);
                free(script_copy);
                return 0;
            }
            continue;
        }

        if (strcmp(cmd, "mod") == 0) {
            char synth_name[DSL_MAX_NAME] = {0};
            char dest_token[32] = {0};
            char src_token[32] = {0};
            char rate_token[32] = {0};
            char depth_token[32] = {0};
            char offset_token[32] = {0};
            char lag_token[32] = {0};
            char slew_token[32] = {0};
            if (!next_token(&cursor, synth_name, sizeof(synth_name), 0) ||
                !next_token(&cursor, dest_token, sizeof(dest_token), 0) ||
                !next_token(&cursor, src_token, sizeof(src_token), 0) ||
                !next_token(&cursor, rate_token, sizeof(rate_token), 0) ||
                !next_token(&cursor, depth_token, sizeof(depth_token), 0)) {
                snprintf(error, error_len, "Line %d: mod requires synth dest source rate depth [offset] [lag] [slew]", line_num);
                free(script_copy);
                return 0;
            }

            int idx = dsl_find_synth(out_program, synth_name);
            if (idx < 0) {
                snprintf(error, error_len, "Line %d: unknown synth '%s'", line_num, synth_name);
                free(script_copy);
                return 0;
            }
            SynthDef *synth = &out_program->synths[idx];
            if (synth->mod_count >= 32) {
                snprintf(error, error_len, "Line %d: too many mods for synth '%s' (max 32)", line_num, synth_name);
                free(script_copy);
                return 0;
            }

            ModDest dest;
            if (strcmp(dest_token, "amp") == 0) dest = MOD_DEST_AMP;
            else if (strcmp(dest_token, "cutoff") == 0) dest = MOD_DEST_CUTOFF;
            else if (strcmp(dest_token, "res") == 0) dest = MOD_DEST_RES;
            else if (strcmp(dest_token, "pan") == 0) dest = MOD_DEST_PAN;
            else if (strcmp(dest_token, "pitch") == 0) dest = MOD_DEST_PITCH;
            else {
                snprintf(error, error_len, "Line %d: unknown mod dest '%s'", line_num, dest_token);
                free(script_copy);
                return 0;
            }

            ModSource src;
            if (strcmp(src_token, "lfo") == 0) src = MOD_SRC_LFO;
            else if (strcmp(src_token, "env") == 0) src = MOD_SRC_ENV;
            else if (strcmp(src_token, "noise") == 0) src = MOD_SRC_NOISE;
            else if (strcmp(src_token, "sample_hold") == 0 || strcmp(src_token, "s&h") == 0) src = MOD_SRC_SAMPLE_HOLD;
            else if (strcmp(src_token, "ring") == 0) src = MOD_SRC_RING;
            else if (strcmp(src_token, "sync") == 0) src = MOD_SRC_SYNC;
            else {
                snprintf(error, error_len, "Line %d: unknown mod source '%s'", line_num, src_token);
                free(script_copy);
                return 0;
            }

            float rate = (float)atof(rate_token);
            float depth = (float)atof(depth_token);
            float offset = 0.0f;
            float lag_ms = 0.0f;
            float slew_ms = 0.0f;

            if (next_token(&cursor, offset_token, sizeof(offset_token), 0)) {
                offset = (float)atof(offset_token);
                if (next_token(&cursor, lag_token, sizeof(lag_token), 0)) {
                    lag_ms = (float)atof(lag_token);
                    if (next_token(&cursor, slew_token, sizeof(slew_token), 0)) {
                        slew_ms = (float)atof(slew_token);
                    }
                }
            }

            ModDef *mod = &synth->mods[synth->mod_count++];
            mod->dest = dest;
            mod->source = src;
            mod->rate = rate;
            mod->depth = depth;
            mod->offset = offset;
            mod->lag_ms = lag_ms;
            mod->slew_ms = slew_ms;
            continue;
        }

        if (strcmp(cmd, "pattern") == 0) {
            if (out_program->pattern_count >= DSL_MAX_PATTERNS) {
                snprintf(error, error_len, "Line %d: too many patterns", line_num);
                free(script_copy);
                return 0;
            }
            char name[DSL_MAX_NAME] = {0};
            char sequence[256] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0) || !next_token(&cursor, sequence, sizeof(sequence), 1)) {
                snprintf(error, error_len, "Line %d: pattern requires name and sequence in () or \"\"", line_num);
                free(script_copy);
                return 0;
            }

            PatternDef *pattern = &out_program->patterns[out_program->pattern_count++];
            memset(pattern, 0, sizeof(*pattern));
            strncpy(pattern->name, name, sizeof(pattern->name) - 1);
            if (!parse_pattern(sequence, pattern, error, error_len, out_program)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Line %d: %s", line_num, error);
                strncpy(error, msg, error_len - 1);
                error[error_len - 1] = '\0';
                free(script_copy);
                return 0;
            }
            if (!pad_pattern_to_timesig(out_program, pattern, error, error_len)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Line %d: %s", line_num, error);
                strncpy(error, msg, error_len - 1);
                error[error_len - 1] = '\0';
                free(script_copy);
                return 0;
            }
            continue;
        }

        if (strcmp(cmd, "accent") == 0) {
            char name[DSL_MAX_NAME] = {0};
            char mask[256] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0) || !next_token(&cursor, mask, sizeof(mask), 1)) {
                snprintf(error, error_len, "Line %d: accent requires pattern name and mask", line_num);
                free(script_copy);
                return 0;
            }
            int idx = dsl_find_pattern(out_program, name);
            if (idx < 0) {
                snprintf(error, error_len, "Line %d: unknown pattern '%s'", line_num, name);
                free(script_copy);
                return 0;
            }
            PatternDef *p = &out_program->patterns[idx];
            char copy[256];
            strncpy(copy, mask, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';
            int i = 0;
            char *c = copy;
            while (*c && i < p->length) {
                while (*c && (isspace((unsigned char)*c) || *c == ',')) c++;
                if (!*c) break;
                char tok[8] = {0};
                int k = 0;
                while (*c && !isspace((unsigned char)*c) && *c != ',') {
                    if (k + 1 < (int)sizeof(tok)) tok[k++] = *c;
                    c++;
                }
                tok[k] = '\0';
                if (strcmp(tok, "1") == 0 || strcmp(tok, "!") == 0 || strcmp(tok, "acc") == 0) {
                    p->accent[i] = 1;
                } else {
                    p->accent[i] = 0;
                }
                i++;
            }
            continue;
        }

        if (strcmp(cmd, "sequence") == 0) {
            if (out_program->sequence_count >= DSL_MAX_SEQUENCES) {
                snprintf(error, error_len, "Line %d: too many sequences", line_num);
                free(script_copy);
                return 0;
            }
            char name[DSL_MAX_NAME] = {0};
            char sequence[256] = {0};
            if (!next_token(&cursor, name, sizeof(name), 0) || !next_token(&cursor, sequence, sizeof(sequence), 1)) {
                snprintf(error, error_len, "Line %d: sequence requires name and list in ()", line_num);
                free(script_copy);
                return 0;
            }

            SequenceDef *seq = &out_program->sequences[out_program->sequence_count++];
            memset(seq, 0, sizeof(*seq));
            strncpy(seq->name, name, sizeof(seq->name) - 1);

            char copy[256];
            strncpy(copy, sequence, sizeof(copy) - 1);
            copy[sizeof(copy) - 1] = '\0';

            char *c = copy;
            while (*c) {
                while (*c && (isspace((unsigned char)*c) || *c == ',')) c++;
                if (!*c) break;

                char token[64] = {0};
                size_t i = 0;
                while (*c && !isspace((unsigned char)*c) && *c != ',') {
                    if (i + 1 < sizeof(token)) token[i++] = *c;
                    c++;
                }
                token[i] = '\0';

                if (seq->count >= DSL_MAX_SEQUENCE_STEPS) {
                    snprintf(error, error_len, "Line %d: sequence too long", line_num);
                    free(script_copy);
                    return 0;
                }

                SequenceStep *step = &seq->steps[seq->count++];
                step->repeat = 1;

                char *aster = strchr(token, '*');
                if (aster) {
                    *aster = '\0';
                    step->repeat = atoi(aster + 1);
                    if (step->repeat < 1) step->repeat = 1;
                }
                strncpy(step->pattern, token, sizeof(step->pattern) - 1);
            }

            if (seq->count == 0) {
                snprintf(error, error_len, "Line %d: sequence needs at least one pattern", line_num);
                free(script_copy);
                return 0;
            }
            continue;
        }

        if (strcmp(cmd, "play") == 0) {
            if (out_program->track_count >= DSL_MAX_TRACKS) {
                snprintf(error, error_len, "Line %d: too many tracks", line_num);
                free(script_copy);
                return 0;
            }
            char pattern[DSL_MAX_NAME] = {0};
            char synth[DSL_MAX_NAME] = {0};
            if (!next_token(&cursor, pattern, sizeof(pattern), 0) || !next_token(&cursor, synth, sizeof(synth), 0)) {
                snprintf(error, error_len, "Line %d: play requires pattern and synth", line_num);
                free(script_copy);
                return 0;
            }

            TrackDef *track = &out_program->tracks[out_program->track_count++];
            memset(track, 0, sizeof(*track));
            strncpy(track->pattern, pattern, sizeof(track->pattern) - 1);
            strncpy(track->synth, synth, sizeof(track->synth) - 1);
            set_default_track(track);

            while (1) {
                char token[32] = {0};
                if (!next_token(&cursor, token, sizeof(token), 0)) {
                    break;
                }
                if (strcmp(token, "rev") == 0) {
                    track->rev = 1;
                    continue;
                }
                if (strcmp(token, "only") == 0) {
                    char range[32] = {0};
                    if (!next_token(&cursor, range, sizeof(range), 0)) {
                        snprintf(error, error_len, "Line %d: only requires a range (e.g., 6-7)", line_num);
                        free(script_copy);
                        return 0;
                    }
                    int start = 0;
                    int end = 0;
                    if (sscanf(range, "%d-%d", &start, &end) != 2) {
                        snprintf(error, error_len, "Line %d: invalid only range '%s'", line_num, range);
                        free(script_copy);
                        return 0;
                    }
                    track->seq_start = start;
                    track->seq_end = end;
                    continue;
                }
                if (strcmp(token, "only") == 0) {
                    char range[32] = {0};
                    if (!next_token(&cursor, range, sizeof(range), 0)) {
                        snprintf(error, error_len, "Line %d: only requires a range (e.g., 6-7)", line_num);
                        free(script_copy);
                        return 0;
                    }
                    int start = 0;
                    int end = 0;
                    if (sscanf(range, "%d-%d", &start, &end) != 2) {
                        snprintf(error, error_len, "Line %d: invalid only range '%s'", line_num, range);
                        free(script_copy);
                        return 0;
                    }
                    track->seq_start = start;
                    track->seq_end = end;
                    continue;
                }
                if (strcmp(token, "orn") == 0 || strcmp(token, "ornament") == 0) {
                    char value[32] = {0};
                    if (!next_token(&cursor, value, sizeof(value), 0)) {
                        snprintf(error, error_len, "Line %d: %s requires a value", line_num, token);
                        free(script_copy);
                        return 0;
                    }
                    track->ornament_prob = (float)atof(value);
                    if (track->ornament_prob < 0.0f) track->ornament_prob = 0.0f;
                    if (track->ornament_prob > 1.0f) track->ornament_prob = 1.0f;
                    char *saved = cursor;
                    char mode[16] = {0};
                    if (next_token(&cursor, mode, sizeof(mode), 0)) {
                        if (strcmp(mode, "up") == 0) track->ornament_mode = 1;
                        else if (strcmp(mode, "down") == 0) track->ornament_mode = 0;
                        else if (strcmp(mode, "alt") == 0) track->ornament_mode = 2;
                        else {
                            cursor = saved; // not a mode; treat as next option
                        }
                    }
                    continue;
                }
                if (strcmp(token, "rate") == 0 || strcmp(token, "fast") == 0 || strcmp(token, "slow") == 0 ||
                    strcmp(token, "every") == 0 || strcmp(token, "density") == 0 || strcmp(token, "hurry") == 0 ||
                    strcmp(token, "iter") == 0 || strcmp(token, "chunk") == 0 || strcmp(token, "stut") == 0 ||
                    strcmp(token, "palindrome") == 0 || strcmp(token, "slide") == 0 || strcmp(token, "acc") == 0) {
                    if (strcmp(token, "palindrome") == 0) {
                        track->palindrome = 1;
                        continue;
                    }
                    char value[32] = {0};
                    if (!next_token(&cursor, value, sizeof(value), 0)) {
                        snprintf(error, error_len, "Line %d: %s requires a value", line_num, token);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "rate") == 0) {
                        track->rate = (float)atof(value);
                    } else if (strcmp(token, "hurry") == 0) {
                        track->hurry = (float)atof(value);
                    } else if (strcmp(token, "fast") == 0) {
                        track->fast = atoi(value);
                    } else if (strcmp(token, "slow") == 0) {
                        track->slow = atoi(value);
                    } else if (strcmp(token, "every") == 0) {
                        track->every = atoi(value);
                    } else if (strcmp(token, "density") == 0) {
                        track->density = (float)atof(value);
                    } else if (strcmp(token, "iter") == 0) {
                        track->iter = atoi(value);
                    } else if (strcmp(token, "chunk") == 0) {
                        track->chunk = atoi(value);
                    } else if (strcmp(token, "stut") == 0) {
                        track->stut = atoi(value);
                    } else if (strcmp(token, "slide") == 0) {
                        track->slide_ms = (float)atof(value);
                    } else if (strcmp(token, "acc") == 0) {
                        track->accent_prob = (float)atof(value);
                    } else if (strcmp(token, "orn") == 0 || strcmp(token, "ornament") == 0) {
                        track->ornament_prob = (float)atof(value);
                    }
                    if (strcmp(token, "rate") == 0 && track->rate <= 0.0f) {
                        snprintf(error, error_len, "Line %d: rate must be > 0", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "hurry") == 0 && track->hurry <= 0.0f) {
                        snprintf(error, error_len, "Line %d: hurry must be > 0", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "slide") == 0 && track->slide_ms < 0.0f) {
                        snprintf(error, error_len, "Line %d: slide must be >= 0", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "acc") == 0) {
                        if (track->accent_prob < 0.0f) track->accent_prob = 0.0f;
                        if (track->accent_prob > 1.0f) track->accent_prob = 1.0f;
                    }
                    if (strcmp(token, "fast") == 0 && track->fast < 1) {
                        snprintf(error, error_len, "Line %d: fast must be >= 1", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "slow") == 0 && track->slow < 1) {
                        snprintf(error, error_len, "Line %d: slow must be >= 1", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "every") == 0 && track->every < 1) {
                        snprintf(error, error_len, "Line %d: every must be >= 1", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "density") == 0) {
                        if (track->density < 0.0f) track->density = 0.0f;
                        if (track->density > 1.0f) track->density = 1.0f;
                    }
                    if (strcmp(token, "iter") == 0 && track->iter < 1) {
                        snprintf(error, error_len, "Line %d: iter must be >= 1", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "chunk") == 0 && track->chunk < 0) {
                        snprintf(error, error_len, "Line %d: chunk must be >= 0", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "stut") == 0 && track->stut < 1) {
                        snprintf(error, error_len, "Line %d: stut must be >= 1", line_num);
                        free(script_copy);
                        return 0;
                    }
                    if ((strcmp(token, "orn") == 0 || strcmp(token, "ornament") == 0)) {
                        if (track->ornament_prob < 0.0f) track->ornament_prob = 0.0f;
                        if (track->ornament_prob > 1.0f) track->ornament_prob = 1.0f;
                    }
                    continue;
                }

                snprintf(error, error_len, "Line %d: unknown play option '%s'", line_num, token);
                free(script_copy);
                return 0;
            }
            continue;
        }

        if (strcmp(cmd, "playseq") == 0) {
            if (out_program->track_count >= DSL_MAX_TRACKS) {
                snprintf(error, error_len, "Line %d: too many tracks", line_num);
                free(script_copy);
                return 0;
            }
            char seq_name[DSL_MAX_NAME] = {0};
            char synth[DSL_MAX_NAME] = {0};
            if (!next_token(&cursor, seq_name, sizeof(seq_name), 0) || !next_token(&cursor, synth, sizeof(synth), 0)) {
                snprintf(error, error_len, "Line %d: playseq requires sequence and synth", line_num);
                free(script_copy);
                return 0;
            }

            TrackDef *track = &out_program->tracks[out_program->track_count++];
            memset(track, 0, sizeof(*track));
            strncpy(track->pattern, seq_name, sizeof(track->pattern) - 1);
            strncpy(track->synth, synth, sizeof(track->synth) - 1);
            set_default_track(track);
            track->is_sequence = 1;

            while (1) {
                char token[32] = {0};
                if (!next_token(&cursor, token, sizeof(token), 0)) {
                    break;
                }
                if (strcmp(token, "rev") == 0) {
                    track->rev = 1;
                    continue;
                }
                if (strcmp(token, "orn") == 0 || strcmp(token, "ornament") == 0) {
                    char value[32] = {0};
                    if (!next_token(&cursor, value, sizeof(value), 0)) {
                        snprintf(error, error_len, "Line %d: %s requires a value", line_num, token);
                        free(script_copy);
                        return 0;
                    }
                    track->ornament_prob = (float)atof(value);
                    if (track->ornament_prob < 0.0f) track->ornament_prob = 0.0f;
                    if (track->ornament_prob > 1.0f) track->ornament_prob = 1.0f;
                    char *saved = cursor;
                    char mode[16] = {0};
                    if (next_token(&cursor, mode, sizeof(mode), 0)) {
                        if (strcmp(mode, "up") == 0) track->ornament_mode = 1;
                        else if (strcmp(mode, "down") == 0) track->ornament_mode = 0;
                        else if (strcmp(mode, "alt") == 0) track->ornament_mode = 2;
                        else {
                            cursor = saved; // not a mode; treat as next option
                        }
                    }
                    continue;
                }
                if (strcmp(token, "palindrome") == 0) {
                    track->palindrome = 1;
                    continue;
                }
                if (strcmp(token, "only") == 0) {
                    char range[32] = {0};
                    if (!next_token(&cursor, range, sizeof(range), 0)) {
                        snprintf(error, error_len, "Line %d: only requires a range (e.g., 6-7)", line_num);
                        free(script_copy);
                        return 0;
                    }
                    int start = 0;
                    int end = 0;
                    if (sscanf(range, "%d-%d", &start, &end) != 2) {
                        snprintf(error, error_len, "Line %d: invalid only range '%s'", line_num, range);
                        free(script_copy);
                        return 0;
                    }
                    track->seq_start = start;
                    track->seq_end = end;
                    continue;
                }
                if (strcmp(token, "rate") == 0 || strcmp(token, "fast") == 0 || strcmp(token, "slow") == 0 ||
                    strcmp(token, "every") == 0 || strcmp(token, "density") == 0 || strcmp(token, "hurry") == 0 ||
                    strcmp(token, "iter") == 0 || strcmp(token, "chunk") == 0 || strcmp(token, "stut") == 0 ||
                    strcmp(token, "slide") == 0 || strcmp(token, "acc") == 0) {
                    char value[32] = {0};
                    if (!next_token(&cursor, value, sizeof(value), 0)) {
                        snprintf(error, error_len, "Line %d: %s requires a value", line_num, token);
                        free(script_copy);
                        return 0;
                    }
                    if (strcmp(token, "rate") == 0) {
                        track->rate = (float)atof(value);
                    } else if (strcmp(token, "hurry") == 0) {
                        track->hurry = (float)atof(value);
                    } else if (strcmp(token, "fast") == 0) {
                        track->fast = atoi(value);
                    } else if (strcmp(token, "slow") == 0) {
                        track->slow = atoi(value);
                    } else if (strcmp(token, "every") == 0) {
                        track->every = atoi(value);
                    } else if (strcmp(token, "density") == 0) {
                        track->density = (float)atof(value);
                    } else if (strcmp(token, "iter") == 0) {
                        track->iter = atoi(value);
                    } else if (strcmp(token, "chunk") == 0) {
                        track->chunk = atoi(value);
                    } else if (strcmp(token, "stut") == 0) {
                        track->stut = atoi(value);
                    } else if (strcmp(token, "slide") == 0) {
                        track->slide_ms = (float)atof(value);
                    } else if (strcmp(token, "acc") == 0) {
                        track->accent_prob = (float)atof(value);
                    } else if (strcmp(token, "orn") == 0 || strcmp(token, "ornament") == 0) {
                        track->ornament_prob = (float)atof(value);
                    }
                    if (strcmp(token, "acc") == 0) {
                        if (track->accent_prob < 0.0f) track->accent_prob = 0.0f;
                        if (track->accent_prob > 1.0f) track->accent_prob = 1.0f;
                    }
                    continue;
                }

                snprintf(error, error_len, "Line %d: unknown playseq option '%s'", line_num, token);
                free(script_copy);
                return 0;
            }
            continue;
        }

        snprintf(error, error_len, "Line %d: unknown command '%s'", line_num, cmd);
        free(script_copy);
        return 0;
    }

    if (out_program->track_count == 0) {
        snprintf(error, error_len, "No play command found");
        free(script_copy);
        return 0;
    }

    free(script_copy);
    return 1;
}
