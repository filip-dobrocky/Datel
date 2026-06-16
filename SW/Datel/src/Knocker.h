#pragma once

#include <Arduino.h>
// Use painlessMesh's TaskScheduler
#include "painlessMesh.h"
#include "Pattern.h"

#ifndef KNOCKER_OFF_TIME_MIN
#define KNOCKER_OFF_TIME_MIN 5UL    // minimum audible windup (ms)
#endif
#ifndef KNOCKER_OFF_TIME_MAX
#define KNOCKER_OFF_TIME_MAX 50UL   // default per-node max windup (ms)
#endif
#ifndef KNOCKER_OFF_TIME_FRAC
#define KNOCKER_OFF_TIME_FRAC 0.5f  // windup capped at this fraction of the interval
#endif

const char* KNOCK_TAG = "Knocker";

class Knocker {
public:
    static Knocker* instance;

    Knocker(uint8_t pin, uint16_t tempo = 200)
    : pin(pin), tempo(tempo)
    {
        pinMode(pin, OUTPUT);
        analogWriteFrequency(pin, 20000);

        scheduler.startNow();

        instance = this;
    };

    void update() {
        scheduler.execute();
    };

    void knock() {
        step_count = parsePattern(pattern, steps, MAX_STEPS);
        if (step_count == 0) return;
        knock_index = 0;
        t_knock.restart();
        if (onStarted) {
            onStarted();
        }
    };

    void peck(float freq, uint32_t dur, float curve, uint8_t amp = 255) {
        peck_interval = (uint32_t)(1000.0f / freq);
        peck_total    = dur / peck_interval;
        peck_step     = 0;
        peck_curve    = curve;
        peck_amp      = amp;
        t_peck.setInterval(peck_interval);
        t_peck.restart();
    };

    void setPattern(const String& new_pattern) {
        pattern = new_pattern;
        step_count = parsePattern(pattern, steps, MAX_STEPS);
    };

    String getPattern() const {
        return pattern;
    };

    void setVelocity(uint8_t new_velocity) {
        velocity = new_velocity;
    };
    
    uint8_t getVelocity() const {
        return velocity;
    };

    void setTempo(uint16_t new_tempo) {
        tempo = new_tempo;
    };

    // Per-object solenoid tuning (NodeConfig), applied at runtime so one
    // firmware image serves hardware variants. Max windup (ms) at full velocity.
    void setOffTimeMax(uint32_t ms) {
        off_time_max = ms;
    };

    uint16_t getTempo() const {
        return tempo;
    };

    void setOnFinished(void (*callback)()) {
        onFinished = callback;
    };

    void setOnStarted(void (*callback)()) {
        onStarted = callback;
    };

private:
    // Windup (retract/ON) duration: how long the solenoid is energized before
    // the release that actually produces the knock. The audible event is the
    // release, so the windup is fired *ahead* of the beat (see knock_callback)
    // and the release lands on the beat. Scales with velocity (more windup +
    // higher duty = harder knock) and is capped to a fraction of the available
    // interval so it always fits before the next beat (articulation).
    //   eff_vel:      0..255 effective amplitude (per-step * master, or peck env)
    //   cap_interval: ms available for this step (note_ms / peck_interval)
    static uint32_t calc_windup(uint32_t eff_vel, uint32_t cap_interval) {
        if (eff_vel == 0) return 0;                 // silent: caller skips the knock
        uint32_t range = instance->off_time_max - KNOCKER_OFF_TIME_MIN;
        uint32_t t = KNOCKER_OFF_TIME_MIN + range * eff_vel / 255;  // velocity -> windup
        uint32_t cap = (uint32_t)(cap_interval * KNOCKER_OFF_TIME_FRAC);
        return min(t, cap);
    }

    // Effective velocity of a step after master gain. Hits only; pecks and rests
    // return 0 so the look-ahead gives them no pre-charge (pecks trigger on the
    // beat, rests are silent).
    static uint8_t step_eff(const Step& st, uint8_t master) {
        if (st.type == STEP_HIT) return (uint16_t)st.velocity * master / 255;
        return 0;
    }

    // Fires at each step's *charge* time, which is windup-ms before its beat so
    // the release (the audible knock) lands exactly on the beat. The beat grid
    // is anchored to the knock, not the energize, so velocity can vary the
    // windup without dragging the rhythm.
    static void knock_callback() {
        Knocker* k = instance;
        uint32_t note_ms = 15000UL / k->tempo; // sixteenth note duration in ms

        const Step& st = k->steps[k->knock_index];
        uint32_t span = (st.type == STEP_PECK) ? st.dur : 1; // steps this struct consumes
        uint8_t  eff  = step_eff(st, k->velocity);
        uint32_t windup = calc_windup(eff, note_ms);

        if (k->t_peck.isEnabled()) {
            // A peck is still ringing: it owns the pin (ring-over). Stay silent
            // this tick to avoid contending for the shared t_off / PWM.
            analogWrite(k->pin, 0);
            windup = 0;
        } else if (st.type == STEP_HIT) {
            if (eff) {
                // Energize (retract) now; release after `windup` lands the knock
                // on the beat. Velocity sets PWM duty = retraction force.
                k->t_off.setInterval(windup);
                analogWrite(k->pin, eff);
                k->t_off.restartDelayed();
            }
            ESP_LOGD(KNOCK_TAG, "Hit %u eff=%u windup=%u", k->knock_index, eff, windup);
        } else if (st.type == STEP_PECK) {
            uint8_t amp = (uint16_t)st.amp * k->velocity / 255; // master gain
            // peck() takes dur in ms; Step stores dur in steps -> convert.
            k->peck(st.freq, (uint32_t)st.dur * note_ms, (float)st.curve, amp);
            ESP_LOGD(KNOCK_TAG, "Peck %u f=%u dur=%u amp=%u", k->knock_index,
                     st.freq, st.dur, amp);
        } else { // STEP_REST
            analogWrite(k->pin, 0);
        }

        k->knock_index++;
        if (k->knock_index < k->step_count) {
            // Pre-fire the next charge so its release lands on the next beat:
            //   next beat   = this charge + windup + span*note_ms
            //   next charge = next beat - windup_next
            uint32_t windup_next = calc_windup(step_eff(k->steps[k->knock_index],
                                                        k->velocity), note_ms);
            long delay = (long)windup + (long)span * note_ms - (long)windup_next;
            if (delay < 5) delay = 5;
            k->t_knock.restartDelayed((uint32_t)delay);
        }
        else {
            k->knock_index = 0;
            k->t_knock.disable();
            if (k->onFinished) {
                k->onFinished();
            }
        }
    }

    static void off_callback() {
        analogWrite(instance->pin, 0);
        ESP_LOGD(KNOCK_TAG, "Knock %u off", instance->knock_index);
    }

    static void peck_callback() {
        uint32_t n = instance->peck_total;
        uint32_t i = instance->peck_step;
        float t = (n > 1) ? (float)i / (float)(n - 1) : 1.0f;
        float c = instance->peck_curve;

        float env;
        if (c == 0.0f)     env = 1.0f;
        else if (c > 0.0f) env = powf(t, c);
        else               env = powf(1.0f - t, -c);

        uint8_t eff = (uint8_t)(instance->peck_amp * env);
        uint32_t windup = calc_windup(eff, instance->peck_interval);
        if (windup) {
            instance->t_off.setInterval(windup);
            analogWrite(instance->pin, eff);   // duty = envelope amplitude
            instance->t_off.restartDelayed();
        }
        ESP_LOGD(KNOCK_TAG, "Peck %u/%u eff=%u windup=%u", i, n, eff, windup);

        instance->peck_step++;
        if (instance->peck_step >= n) {
            instance->t_peck.disable();
        }
    }

    Scheduler scheduler;

    String pattern { "xxx_" };
    Step    steps[MAX_STEPS];
    uint8_t step_count = 0;

    Task t_knock { 300UL, TASK_ONCE, &Knocker::knock_callback, &scheduler, false };
    Task t_off   { 25UL, TASK_ONCE, &Knocker::off_callback, &scheduler, false };
    Task t_peck  { 50UL, TASK_FOREVER, &Knocker::peck_callback, &scheduler, false };

    uint8_t pin;
    uint16_t tempo;
    uint32_t off_time_max = KNOCKER_OFF_TIME_MAX;

    uint8_t velocity = 255;  // master gain applied to every hit/peck (0..255)

    uint8_t  knock_index    = 0;
    uint32_t peck_interval  = 50;
    uint32_t peck_step      = 0;
    uint32_t peck_total     = 0;
    float    peck_curve     = 0.0f;
    uint8_t  peck_amp       = 255;

    void (*onStarted)() = nullptr;
    void (*onFinished)() = nullptr;
};

Knocker* Knocker::instance = nullptr;
