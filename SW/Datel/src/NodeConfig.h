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
    // Solenoid max windup in ms at full velocity (Knocker::calc_off_time).
    uint32_t knocker_off_time_max;
    // Amplitude floors/defaults (Pattern.h g_* runtime tuning).
    uint8_t hit_amp_min;       // floor for hit velocity (audible solenoid)
    uint8_t peck_amp_min;      // floor for peck amplitude
    uint8_t hit_amp_default;   // velocity for a bare "x"
    uint8_t peck_amp_default;  // amp for a default-built peck
};

inline NodeConfig nodeConfigFor(int id) {
    NodeConfig cfg{50, HIT_AMP_MIN, PECK_AMP_MIN, HIT_AMP_DEFAULT,
                   PECK_AMP_DEFAULT};
    switch (id) {
        case 7:
        case 10:
            // Different solenoid hardware: needs a longer windup.
            // cfg.knocker_off_time_max = 80;
            break;
    }
    return cfg;
}
