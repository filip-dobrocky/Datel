#pragma once

#include <Arduino.h>
// Use painlessMesh's TaskScheduler
#include "painlessMesh.h"

#define OFF_TIME 50UL

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
        t_knock.restart();
        if (onStarted) {
            onStarted();
        }
    };

    void setPattern(const String& new_pattern) {
        pattern = new_pattern;
    };

    void setVelocity(uint8_t new_velocity) {
        velocity = new_velocity;
    };

    void setTempo(uint16_t new_tempo) {
        tempo = new_tempo;
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
            analogWrite(instance->pin, instance->velocity);
            instance->t_off.restartDelayed();
            ESP_LOGD(KNOCK_TAG, "Knock %u %u", instance->knock_index, instance->velocity);
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

    Scheduler scheduler;

    String pattern { "xxx_" };

    Task t_knock { 300UL, TASK_ONCE, &Knocker::knock_callback, &scheduler, false };
    Task t_off { OFF_TIME, TASK_ONCE, &Knocker::off_callback, &scheduler, false };

    uint8_t pin;
    uint16_t tempo;

    uint8_t velocity = 255;

    uint8_t knock_index = 0;

    void (*onStarted)() = nullptr;
    void (*onFinished)() = nullptr;
};

Knocker* Knocker::instance = nullptr;
