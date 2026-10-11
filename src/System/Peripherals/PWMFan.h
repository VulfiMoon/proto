#pragma once

#include <Arduino.h>
#include "Typedefs.h"
#include "config.h"
#include "System/Utilities/Util.h"

/**
 * 4-wire PWM fan controller.
 *
 * Speed values are 0-255 like everywhere else in the firmware, but they are mapped
 * onto the full duty cycle range of the configured LEDC resolution, so "255" always
 * means a permanently high PWM line (= the fan's rated speed, e.g. 6000 RPM).
 *
 * On top of that:
 *  - Config::FAN_MIN_DUTY / Config::FAN_MAX_DUTY limit the duty range on purpose
 *    (leave MAX at 1.0 unless you want to cap the fan below its rated speed)
 *  - a full speed "kick" is generated on boot and every time the fan is turned back
 *    on (Config::FAN_SPINUP_DURATION). Small fans often do not spin up straight to a
 *    low duty cycle, which reads exactly like "my fan never reaches full speed"
 *  - optional RPM measurement from the fan's tach wire (Config::FAN_TACHOMETER_ENABLED)
 *
 * Call update() from the main loop, it takes care of the spin up timer and the RPM
 * measurement. Without calling it the fan still runs, it just never leaves the kick.
 */
class PWMFan {
  private:
    u8 pin;
    u8 channel;
    u8 speed;           // requested speed, 0-255
    u8 appliedSpeed;    // speed currently written to the LEDC peripheral
    u32 maxDuty;        // (2 ^ FAN_PWM_RESOLUTION) - 1
    bool spinUp;        // true while the start up kick is running
    u32 spinUpEnd;      // millis() value the kick ends at
    bool tachometer;    // true if RPM measurement is wired up and running
    u16 rpm;            // last measured RPM, 0 if unknown / not measured
    u32 lastRpmUpdate;
    u32 pulsesAtLastUpdate;

    // pulse counter for the tachometer interrupt, kept in a function local static so
    // that this header can be included from more than one translation unit
    static volatile u32& pulseCounter() {
        static volatile u32 counter = 0;
        return counter;
    }

    static void IRAM_ATTR onPulse() {
        pulseCounter()++;
    }

    // maps a 0-255 speed value onto [FAN_MIN_DUTY .. FAN_MAX_DUTY] of the LEDC range
    u32 dutyFromSpeed(u8 value) {
        f32 t = (f32)value / 255.0f;
        f32 duty = Config::FAN_MIN_DUTY + t * (Config::FAN_MAX_DUTY - Config::FAN_MIN_DUTY);
        if(duty < 0.0f) duty = 0.0f;
        if(duty > 1.0f) duty = 1.0f;
        u32 ret = (u32)(duty * (f32)maxDuty + 0.5f);
        if(ret > maxDuty) ret = maxDuty;
        if(Config::FAN_PWM_INVERTED) ret = maxDuty - ret;
        return ret;
    }

    void writeSpeed(u8 value) {
        appliedSpeed = value;
        ledcWrite(channel, dutyFromSpeed(value));
    }

    void startSpinUp() {
        if(Config::FAN_SPINUP_DURATION == 0) {
            spinUp = false;
            writeSpeed(speed);
            return;
        }
        spinUp = true;
        spinUpEnd = millis() + Config::FAN_SPINUP_DURATION;
        writeSpeed(255);
    }

  public:
    PWMFan(u8 pin, u8 channel, u8 speed = 255)
        : pin(pin)
        , channel(channel)
        , speed(speed)
        , appliedSpeed(speed)
        , spinUp(false)
        , spinUpEnd(0)
        , tachometer(false)
        , rpm(0)
        , lastRpmUpdate(0)
        , pulsesAtLastUpdate(0)
    {
        maxDuty = (1UL << Config::FAN_PWM_RESOLUTION) - 1;

        // FAN_PWM_FREQUENCY is the requested frequency, the LEDC peripheral rounds it
        // to the nearest value it can produce from its clock. Printed when -DDEBUG is on.
        u32 actualFrequency = ledcSetup(channel, Config::FAN_PWM_FREQUENCY, Config::FAN_PWM_RESOLUTION);
        ledcAttachPin(pin, channel);

        // Optional: drive the PWM line open drain instead of push-pull. The PWM input of
        // a 4-wire fan is an open collector input pulled up inside the fan, so with the
        // pin in open drain mode the fan's own pull up defines the high level instead of
        // the ESP32's 3.3V. Only enable this if your fan does not reach full speed with
        // 3.3V logic. NOTE: the pin will then see the fan's pull up voltage, check that
        // it is 3.3V (or add a divider) before enabling this.
        if(Config::FAN_PWM_OPEN_DRAIN) pinMode(pin, OUTPUT_OPEN_DRAIN);

        tachometer = Config::FAN_TACHOMETER_ENABLED;
        if(tachometer) {
            pinMode(Config::Pins::FAN_TACH, INPUT_PULLUP);
            attachInterrupt(digitalPinToInterrupt(Config::Pins::FAN_TACH), onPulse, FALLING);
            lastRpmUpdate = millis();
            pulsesAtLastUpdate = pulseCounter();
        }

        TOASTER_LOG(
            "PWMFan: pin %d, channel %d, resolution %d bit (max duty %u), %u Hz requested / %u Hz actual, duty range %d%% - %d%%, tachometer %s\n",
            pin, channel, Config::FAN_PWM_RESOLUTION, maxDuty, Config::FAN_PWM_FREQUENCY, actualFrequency,
            (u16)(Config::FAN_MIN_DUTY * 100.0f), (u16)(Config::FAN_MAX_DUTY * 100.0f), tachometer ? "on" : "off"
        );

        startSpinUp();   // always start the fan with a full speed kick
    }

    u8 getSpeed() {
        return speed;
    }

    // speed as a percentage, handy for the HUD
    u8 getSpeedPercent() {
        return (u8)(((u16)speed * 100) / 255);
    }

    void setSpeed(u8 speed) {
        bool wasOff = this->appliedSpeed == 0;
        this->speed = speed;
        if(spinUp && speed == 255) return;          // already running at full speed
        if(wasOff && speed > 0) {                   // restarting from a standstill
            startSpinUp();
            return;
        }
        spinUp = false;
        writeSpeed(speed);
    }

    // true while the start up kick is running, the fan is at 100% regardless of the setting
    bool isSpinningUp() {
        return spinUp;
    }

    bool hasTachometer() {
        return tachometer;
    }

    // last measured RPM, 0 when the fan is stopped or the tachometer is disabled
    u16 getRPM() {
        return rpm;
    }

    void update() {
        if(spinUp && millis() >= spinUpEnd) {
            spinUp = false;
            writeSpeed(speed);
        }

        if(!tachometer) return;

        u32 now = millis();
        u32 elapsed = now - lastRpmUpdate;
        if(elapsed < Config::FAN_RPM_UPDATE_INTERVAL) return;

        u32 pulses = pulseCounter();
        u32 delta = pulses - pulsesAtLastUpdate;
        pulsesAtLastUpdate = pulses;
        lastRpmUpdate = now;

        if(delta == 0) {
            rpm = 0;
            return;
        }
        // pulses / FAN_PULSES_PER_REVOLUTION revolutions in `elapsed` milliseconds
        u32 newRpm = ((u32)delta * 60000UL) / ((u32)Config::FAN_PULSES_PER_REVOLUTION * elapsed);
        rpm = newRpm > 0xFFFF ? 0xFFFF : (u16)newRpm;
    }
};
