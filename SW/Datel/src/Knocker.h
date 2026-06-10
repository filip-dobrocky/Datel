#pragma once

#include <Arduino.h>
// Use painlessMesh's TaskScheduler
#include "painlessMesh.h"
#include "Pattern.h"

#ifndef KNOCKER_OFF_TIME_SCALE
#define KNOCKER_OFF_TIME_SCALE 500UL
#endif
#ifndef KNOCKER_OFF_TIME_MIN
#define KNOCKER_OFF_TIME_MIN 5UL
#endif
#ifndef KNOCKER_OFF_TIME_MAX
#define KNOCKER_OFF_TIME_MAX 50UL
#endif
#ifndef KNOCKER_ON_TIME_MIN
#define KNOCKER_ON_TIME_MIN 10UL
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
    // firmware image serves hardware variants.
    void setOffTimeScale(uint32_t scale) {
        off_time_scale = scale;
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
    static uint32_t calc_off_time(uint32_t interval) {
        uint32_t t = (interval * interval) / instance->off_time_scale;
        t = max(t, KNOCKER_OFF_TIME_MIN);
        t = min(t, KNOCKER_OFF_TIME_MAX);
        t = min(t, interval > KNOCKER_ON_TIME_MIN ? interval - KNOCKER_ON_TIME_MIN : KNOCKER_OFF_TIME_MIN);
        return t;
    }

    static void knock_callback() {
        Knocker* k = instance;
        uint32_t note_ms = 15000UL / k->tempo; // sixteenth note duration in ms
        uint32_t span = 1;                     // musical steps this struct consumes

        const Step& st = k->steps[k->knock_index];

        if (k->t_peck.isEnabled()) {
            // A peck is still ringing: it owns the pin (ring-over). Stay silent
            // this tick to avoid contending for the shared t_off / PWM.
            analogWrite(k->pin, 0);
        } else if (st.type == STEP_HIT) {
            uint8_t out = (uint16_t)st.velocity * k->velocity / 255; // master gain
            k->t_off.setInterval(calc_off_time(note_ms));
            analogWrite(k->pin, out);
            k->t_off.restartDelayed();
            ESP_LOGD(KNOCK_TAG, "Hit %u vel=%u", k->knock_index, out);
        } else if (st.type == STEP_PECK) {
            uint8_t amp = (uint16_t)st.amp * k->velocity / 255; // master gain
            // peck() takes dur in ms; Step stores dur in steps -> convert.
            k->peck(st.freq, (uint32_t)st.dur * note_ms, (float)st.curve, amp);
            span = st.dur;
            ESP_LOGD(KNOCK_TAG, "Peck %u f=%u dur=%u amp=%u", k->knock_index,
                     st.freq, st.dur, amp);
        } else { // STEP_REST
            analogWrite(k->pin, 0);
        }

        k->knock_index++;
        if (k->knock_index < k->step_count) {
            uint32_t interval = span * note_ms;
            if (interval < 30) interval = 30;
            k->t_knock.restartDelayed(interval);
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

        uint8_t vel = (uint8_t)(instance->peck_amp * env);
        uint32_t off_time = calc_off_time(instance->peck_interval);
        instance->t_off.setInterval(off_time);
        analogWrite(instance->pin, vel);
        instance->t_off.restartDelayed();
        ESP_LOGD(KNOCK_TAG, "Peck %u/%u vel=%u", i, n, vel);

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
    uint32_t off_time_scale = KNOCKER_OFF_TIME_SCALE;

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
