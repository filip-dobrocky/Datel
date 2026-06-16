#pragma once
#include <Arduino.h>

#include "Pattern.h"  // compile-time defaults for the amp fields

// =====================================================================
//  Per-object runtime constants
//
//  One generic firmware image serves all nodes (mesh OTA), so values
//  that used to be per-env -D build flags live here instead, keyed by
//  the NVS-loaded object id. setup() applies them (Knocker setter +
//  the g_* runtime amps in Pattern.h).
//
//  The per-id overrides below come from limited testing; tune per
//  solenoid as needed. Structural constants every node must agree on
//  (MAX_STEPS, the notation itself) stay compile-time.
// =====================================================================

struct NodeConfig {
    // Solenoid max windup in ms at full velocity (Knocker::calc_windup).
    uint32_t knocker_off_time_max;
    // Windup ceiling as a fraction of the step/sub-hit interval. This (not the
    // ms max) is what bounds the windup at high frequency, so lower it for
    // solenoids that stick / don't release in time when buzzing fast.
    float knocker_off_time_frac;
    // Amplitude floors/defaults (Pattern.h g_* runtime tuning).
    uint8_t hit_amp_min;       // floor for hit velocity (audible solenoid)
    uint8_t peck_amp_min;      // floor for peck amplitude
    uint8_t hit_amp_default;   // velocity for a bare "x"
    uint8_t peck_amp_default;  // amp for a default-built peck
};

inline NodeConfig nodeConfigFor(int id) {
    NodeConfig cfg{50, 0.5f, HIT_AMP_MIN, PECK_AMP_MIN, HIT_AMP_DEFAULT,
                   PECK_AMP_DEFAULT};
    switch (id) {
        case 1:
            cfg.knocker_off_time_frac = 0.2f;
            break;
        case 0:
        case 4:
            cfg.knocker_off_time_frac = 0.3f;
            break;
    }
    return cfg;
}
