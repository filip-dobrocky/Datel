#pragma once
#include <Arduino.h>

// Shared rhythm-pattern notation for Datel. A pattern is a sequence of steps;
// each step is one of three types and occupies one or more 16th-note slots.
//
// Serialized grammar (markers '_' 'x' 'p' are non-hex, so every hex field is
// self-delimiting and fixed-width; hex is uppercase):
//
//   rest : "_"                 one step
//   hit  : "x"                 one step, velocity = HIT_AMP_DEFAULT
//        | "x" HH              one step, HH = velocity byte (clamp >= HIT_AMP_MIN)
//   peck : "p" F D CC AA       occupies `dur` steps
//            F  = 1 nibble  freq-5    (0..15  -> 5..20 Hz)
//            D  = 1 nibble  dur-1     (0..15  -> 1..16 steps)
//            CC = 2 nibbles curve+10  (00..14 -> -10..10)
//            AA = 2 nibbles amp       (00..FF, clamp >= PECK_AMP_MIN)
//
// The parser skips whitespace (for human-authored literals) and is OOB-safe:
// any malformed / truncated token at end-of-string is dropped.

// ---- Tunable limits / amplitude floors (overridable via -D build flags) ----
#ifndef MAX_STEPS
#define MAX_STEPS 32          // max Step structs per pattern
#endif
#ifndef HIT_AMP_MIN
#define HIT_AMP_MIN 60        // floor for hit velocity (audible solenoid)
#endif
#ifndef PECK_AMP_MIN
#define PECK_AMP_MIN 128      // floor for peck amplitude
#endif
#ifndef HIT_AMP_DEFAULT
#define HIT_AMP_DEFAULT 200   // velocity for a bare "x"
#endif
#ifndef PECK_AMP_DEFAULT
#define PECK_AMP_DEFAULT 128
#endif
#ifndef PECK_FREQ_DEFAULT
#define PECK_FREQ_DEFAULT 12  // Hz
#endif
#ifndef PECK_DUR_DEFAULT
#define PECK_DUR_DEFAULT 4    // steps
#endif
#ifndef PECK_CURVE_DEFAULT
#define PECK_CURVE_DEFAULT 0
#endif

#define PECK_FREQ_MIN 5
#define PECK_FREQ_MAX 20

// ---- Runtime amplitude tuning (per-object; see NodeConfig.h) ----
// One firmware image serves all nodes, so per-solenoid amp floors/defaults are
// runtime variables, initialized from the compile-time defaults above and
// overridden in setup() once the NVS object id is known.
inline uint8_t g_hit_amp_min      = HIT_AMP_MIN;
inline uint8_t g_peck_amp_min     = PECK_AMP_MIN;
inline uint8_t g_hit_amp_default  = HIT_AMP_DEFAULT;
inline uint8_t g_peck_amp_default = PECK_AMP_DEFAULT;

enum StepType : uint8_t { STEP_REST = 0, STEP_HIT = 1, STEP_PECK = 2 };

struct Step {
    StepType type;
    uint8_t  velocity;   // HIT: 0..255 (>= HIT_AMP_MIN when explicit)
    uint8_t  freq;       // PECK: 5..20 Hz
    uint8_t  dur;        // PECK: 1..16 steps occupied
    int8_t   curve;      // PECK: -10..10
    uint8_t  amp;        // PECK: PECK_AMP_MIN..255
};

// ---- Clamp helpers (used on decode and on mutation) ----
inline uint8_t clampHitVel(int v)  { if (v < g_hit_amp_min)  v = g_hit_amp_min;  if (v > 255) v = 255; return (uint8_t)v; }
inline uint8_t clampPeckAmp(int a) { if (a < g_peck_amp_min) a = g_peck_amp_min; if (a > 255) a = 255; return (uint8_t)a; }
inline uint8_t clampFreq(int f)    { if (f < PECK_FREQ_MIN) f = PECK_FREQ_MIN; if (f > PECK_FREQ_MAX) f = PECK_FREQ_MAX; return (uint8_t)f; }
inline uint8_t clampDur(int d)     { if (d < 1) d = 1; if (d > 16) d = 16; return (uint8_t)d; }
inline int8_t  clampCurve(int c)   { if (c < -10) c = -10; if (c > 10) c = 10; return (int8_t)c; }

// ---- Hex helpers ----
inline int  hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
inline bool isHexCh(char c)   { return hexVal(c) >= 0; }
inline char hexDigit(uint8_t v) { return "0123456789ABCDEF"[v & 0x0F]; }

// Build a peck step from defaults.
inline Step makeDefaultPeck() {
    Step st;
    st.type     = STEP_PECK;
    st.velocity = g_hit_amp_default;
    st.freq     = PECK_FREQ_DEFAULT;
    st.dur      = PECK_DUR_DEFAULT;
    st.curve    = PECK_CURVE_DEFAULT;
    st.amp      = g_peck_amp_default;
    return st;
}

// Parse a serialized pattern into `out`. Returns the step count (<= maxSteps).
inline uint8_t parsePattern(const String &s, Step *out, uint8_t maxSteps) {
    uint8_t count = 0;
    int len = s.length();
    int i = 0;
    while (i < len && count < maxSteps) {
        char c = s[i];
        if (c == '_') {
            out[count].type = STEP_REST;
            count++;
            i++;
        } else if (c == 'x' || c == 'X') {
            Step &st = out[count];
            st.type = STEP_HIT;
            i++;
            // Optional velocity: two hex nibbles immediately following.
            if (i + 1 < len && isHexCh(s[i]) && isHexCh(s[i + 1])) {
                st.velocity = clampHitVel((hexVal(s[i]) << 4) | hexVal(s[i + 1]));
                i += 2;
            } else {
                st.velocity = g_hit_amp_default;
            }
            count++;
        } else if (c == 'p' || c == 'P') {
            // Need 6 hex nibbles: F D CC AA (indices i+1 .. i+6).
            if (i + 6 < len && isHexCh(s[i + 1]) && isHexCh(s[i + 2]) &&
                isHexCh(s[i + 3]) && isHexCh(s[i + 4]) && isHexCh(s[i + 5]) &&
                isHexCh(s[i + 6])) {
                Step &st = out[count];
                st.type  = STEP_PECK;
                st.freq  = clampFreq(hexVal(s[i + 1]) + PECK_FREQ_MIN);
                st.dur   = clampDur(hexVal(s[i + 2]) + 1);
                int cc   = (hexVal(s[i + 3]) << 4) | hexVal(s[i + 4]);
                st.curve = clampCurve(cc - 10);
                int aa   = (hexVal(s[i + 5]) << 4) | hexVal(s[i + 6]);
                st.amp   = clampPeckAmp(aa);
                count++;
                i += 7;
            } else {
                // Malformed / truncated peck: drop the marker.
                i++;
            }
        } else {
            // Whitespace or unknown: skip.
            i++;
        }
    }
    return count;
}

// Serialize steps back to a string. A hit at HIT_AMP_DEFAULT emits a bare "x".
inline String serializePattern(const Step *steps, uint8_t count) {
    String s;
    s.reserve(count * 7 + 1);
    for (uint8_t i = 0; i < count; i++) {
        const Step &st = steps[i];
        if (st.type == STEP_REST) {
            s += '_';
        } else if (st.type == STEP_HIT) {
            s += 'x';
            if (st.velocity != g_hit_amp_default) {
                s += hexDigit(st.velocity >> 4);
                s += hexDigit(st.velocity & 0x0F);
            }
        } else { // STEP_PECK
            uint8_t cc = (uint8_t)(st.curve + 10);
            s += 'p';
            s += hexDigit((st.freq - PECK_FREQ_MIN) & 0x0F);
            s += hexDigit((st.dur - 1) & 0x0F);
            s += hexDigit(cc >> 4);
            s += hexDigit(cc & 0x0F);
            s += hexDigit(st.amp >> 4);
            s += hexDigit(st.amp & 0x0F);
        }
    }
    return s;
}

// Total musical length = sum of per-step durations (rest/hit = 1, peck = dur).
inline uint16_t musicalLength(const Step *steps, uint8_t count) {
    uint16_t total = 0;
    for (uint8_t i = 0; i < count; i++)
        total += (steps[i].type == STEP_PECK) ? steps[i].dur : 1;
    return total;
}
