#pragma once

#include <Arduino.h>
// Use painlessMesh's TaskScheduler
#include "painlessMesh.h"

#define OFF_TIME 25UL

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
        knock_index = 0;
        current_velocity = velocity;  // Reset to full velocity at start
        t_knock.restart();
        if (onStarted) {
            onStarted();
        }
    };

    void peck(float freq, uint32_t dur, float curve) {
        peck_interval = (uint32_t)(1000.0f / freq);
        peck_total    = dur / peck_interval;
        peck_step     = 0;
        peck_curve    = curve;
        t_peck.setInterval(peck_interval);
        t_peck.restart();
    };

    void setPattern(const String& new_pattern) {
        pattern = new_pattern;
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
    static void knock_callback() {
        if (instance->pattern[instance->knock_index] == 'x') {
            analogWrite(instance->pin, instance->current_velocity);
            instance->t_off.restartDelayed();
            ESP_LOGD(KNOCK_TAG, "Knock %u vel=%u", instance->knock_index, instance->current_velocity);
            
            // Decrease velocity for next knock (decay effect)
            if (instance->current_velocity > 2) {
                instance->current_velocity -= 2;
            } else {
                instance->current_velocity = 0;
            }
        } else {
            analogWrite(instance->pin, 0);
        }

        instance->knock_index++;
        if (instance->knock_index < instance->pattern.length()) {
            auto interval = 15000UL / instance->tempo; // sixteenth note duration in ms
            if (interval < OFF_TIME + 50) {
                interval = OFF_TIME + 50;
            }
            instance->t_knock.restartDelayed(interval);
        }
        else {
            instance->knock_index = 0;
            instance->t_knock.disable();
            if (instance->onFinished) {
                instance->onFinished();
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

        uint8_t vel = (uint8_t)(instance->velocity * env);
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

    Task t_knock { 300UL, TASK_ONCE, &Knocker::knock_callback, &scheduler, false };
    Task t_off   { OFF_TIME, TASK_ONCE, &Knocker::off_callback, &scheduler, false };
    Task t_peck  { 50UL, TASK_FOREVER, &Knocker::peck_callback, &scheduler, false };

    uint8_t pin;
    uint16_t tempo;

    uint8_t velocity = 255;
    uint8_t current_velocity = 255;

    uint8_t  knock_index    = 0;
    uint32_t peck_interval  = 50;
    uint32_t peck_step      = 0;
    uint32_t peck_total     = 0;
    float    peck_curve     = 0.0f;

    void (*onStarted)() = nullptr;
    void (*onFinished)() = nullptr;
};

Knocker* Knocker::instance = nullptr;
