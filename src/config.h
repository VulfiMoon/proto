#pragma once

#include "Typedefs.h"
#include "Arduino.h"
#include <vector>

/**
 * Configuration constants for used for setup.
 */

 
// Bluetooth Gamepad selection
// To avoid connection issues, erase flash before changing gamepad type
// (PlatformIO menu -> Project Tasks -> esp32dev -> Platform -> Erase flash)
// If for any reason your bluetooth gamepad name does not match, you can change it in System/Peripherals/Gamepad/InputConfig_xxx

#define GAMEPAD_MOCUTE052F       // mocute 052f gamepad
//#define GAMEPAD_XBOX             // Xbox Wireless gamepad
//#define GAMEPAD_DEBUG            // debug gamepad that prints out HID data


namespace Config {
    // Pin numbers for components
    namespace Pins {
        const u8 
        // Built-in LED
        LED_BUILTIN = 2,
        // Main outer display
        MATRIX_DIN = 23,
        MATRIX_CS = 5,
        MATRIX_CLK = 18,
        // Infrared (boop) sensor
        BOOP_DIGITAL = 19,
        BOOP_ANALOG = 39,
        // Heads up display
        HUD_SCL = 22,
        HUD_SDA = 21,
        // Microphone
        MIC = 36,
        // Fan PWM control
        FAN_PWM = 16,
        // Fan tachometer (RPM) input, only used when Config::FAN_TACHOMETER_ENABLED is true.
        // The tach line is an open collector output, usually pulled up to 5V inside the fan -
        // use a voltage divider (or a 3.3V zener) here, ESP32 pins are not 5V tolerant!
        FAN_TACH = 17;
    }

    namespace Displays {
        const u8 
        NUM_DISPLAYS = 16;     // number of chained MAX7219 devices
        
        // Chain order for this head (from ESP32 towards the end of the chain):
        // EYE(2) -> MOUTH(4) -> NOSE(1) -> MOUTH_FRONT(2) -> NOSE(1) -> MOUTH(4) -> EYE(2)
        // NOTE: Matrix_Max7219::drawColumn() indexes modules starting from the FARTHEST
        // module in the chain (index 0), so the module closest to the ESP32 gets the
        // highest position value. That is why POS_EYE_L (first module wired to the ESP32)
        // has the largest offset below, and POS_EYE_R (last module in the chain) is 0.
        const u8 
        POS_EYE_R =        0,    // eye right       (2 matrices, farthest from ESP32)
        POS_MOUTH_R =      16,   // mouth right     (4 matrices)
        POS_NOSE_R =       48,   // nose right      (1 matrix)
        POS_MOUTH_FRONT =  56,   // front mouth     (2 matrices, inserted between the noses)
        POS_NOSE_L =       72,   // nose left       (1 matrix)
        POS_MOUTH_L =      80,   // mouth left      (4 matrices)
        POS_EYE_L =        112;  // eye left        (2 matrices, closest to ESP32)

        // Display Bitmasks (identifier)
        // Leave these unless you want to add or remap displays
        // If more than 7 displays are required, change the type of DisplayBitmask in Typedefs.h to u16 or u32
        const DisplayBitmask
        EYE_L =         1,        // eye left
        EYE_R =         1 << 1,   // eye right
        MOUTH_L =       1 << 2,   // mouth left
        MOUTH_R =       1 << 3,   // mouth right
        NOSE_L =        1 << 4,   // nose left
        NOSE_R =        1 << 5,   // nose right
        MOUTH_FRONT =   1 << 6,   // extra front-facing mouth panel
        BITMAPBUFFER =  1 << ((sizeof(DisplayBitmask) * 8) - 1),  // bitmap buffer, reserves the leftmost bit
        // Display Bitmask groups
        DEFAULT_DISPLAY = EYE_L,    // default display, set this to any SINGLE display that's in use as some effects depend on it
        EYES = EYE_L | EYE_R,
        NOSE = NOSE_L | NOSE_R,
        // MOUTH_FRONT is driven separately by a global effect in main.cpp (stitches
        // together the bordering columns of MOUTH_L/MOUTH_R), so it's intentionally
        // NOT part of this group anymore.
        MOUTH = MOUTH_L | MOUTH_R,
        LEFT_SIDE = EYE_L | NOSE_L | MOUTH_L,
        RIGHT_SIDE = EYE_R | NOSE_R | MOUTH_R,
        ALL = EYE_L | EYE_R | NOSE_L | NOSE_R | MOUTH_L | MOUTH_R | MOUTH_FRONT;

        // Front mouth panel stitching -------------------------------------------
        // MOUTH_FRONT has no artwork of its own. Every frame it is rebuilt from the
        // 8 columns (one 8x8 block) of MOUTH_R / MOUTH_L that physically border it
        // through the noses, so the mouth reads as one line running around the front
        // of the muzzle. Important: the blocks MUST be the ones next to the nose -
        // those are also the loudest columns of the talking animation (AnimatedMouth
        // opens the mouth widest at the nose end), which is what makes the front
        // panel move while speaking. Taking the wrong (cheek side) block is what
        // makes the front matrices look frozen.
        //
        // Block numbering is the same one the bitmaps use: block 0 = image columns
        // 0..7, block 3 = image columns 24..31. With the chain order documented
        // above (and image x running backwards along the chain):
        //   MOUTH_R: block 3 (columns 24..31) touches NOSE_R  -> front of the mouth
        //   MOUTH_L: block 0 (columns  0.. 7) touches NOSE_L  -> front of the mouth
        const u8
        MOUTH_FRONT_SOURCE_BLOCK_R = 3,     // block of MOUTH_R that borders NOSE_R (0..3)
        MOUTH_FRONT_SOURCE_BLOCK_L = 0;     // block of MOUTH_L that borders NOSE_L (0..3)

        const bool
        MOUTH_FRONT_MIRROR_BLOCKS = true,   // true: the copied block is mirrored, so the line
                                            // flows into the panel and meets in the middle
                                            // (this is what you normally want)
        MOUTH_FRONT_SWAP_HALVES = false,    // true: swap the left/right half of the panel
        MOUTH_FRONT_FLIP_VERTICAL = false;  // true: flip the copied art upside down (only if the
                                            // front modules are mounted rotated compared to the
                                            // side mouths)
    }

    // Miscellaneous settings, best to leave these as is
    const u16 FPS_LIMIT = 120;  // Framerate is limited to avoid fixed point number inaccuracy related issues
    const Timestamp FRAMETIME = 1000000 / FPS_LIMIT;

    // default duration for keyframes
    const Timestamp 
    DEFAULT_KEYFRAME_DURATION = 500 MILLIS,
    DEFAULT_OVERLAY_KEYFRAME_DURATION = 0;

    const u8 
    BOOP_TRIGGER_COUNT = 4,             // how many updates to wait until boop animation triggers
    BOOP_TRIGGERS_MAX = 6,              // maximum value for boop activation (must be higher than activation threshold)
    FAN_PWM_CHANNEL = 0,
    FAN_PWM_RESOLUTION = 8;             // bits of PWM duty resolution (duty = 0..2^resolution-1)

    // Fan ----------------------------------------------------------------
    // Everything here is about getting the fan to actually reach its rated speed.
    // The fan speed setting (0-255) is mapped onto [FAN_MIN_DUTY .. FAN_MAX_DUTY],
    // so with the values below 255 == a permanently high PWM line == full speed.
    const u32 FAN_PWM_FREQUENCY = 25000;        // 25 kHz, Intel 4-wire PWM spec (21-28 kHz is accepted by most fans)
    const bool FAN_PWM_INVERTED = false;        // true if your fan / driver board inverts the signal
    const bool FAN_PWM_OPEN_DRAIN = false;      // true = drive the PWM pin open drain (see README: only useful when the
                                                // fan's PWM line is pulled up to 5V and 3.3V logic is not enough)
    const f32 FAN_MIN_DUTY = 0.0f;              // duty cycle at speed 1 (0.0 - 1.0). Raise this if your fan stalls
                                                // instead of running slowly (0.2 - 0.3 is typical)
    const f32 FAN_MAX_DUTY = 1.0f;              // duty cycle at speed 255. KEEP AT 1.0 - this is what gives you the
                                                // full 6000 RPM. Anything lower caps the fan on purpose.
    const u16 FAN_SPINUP_DURATION = 2000;       // ms of 100% duty on boot and every time the fan is turned back on.
                                                // Small fans often do not spin up to a low duty cycle from a standstill,
                                                // which looks exactly like "the fan never reaches full speed".
    const bool FAN_TACHOMETER_ENABLED = true;   // true = measure real RPM from the fan's tach wire.
                                                // Requires the yellow sense wire on Config::Pins::FAN_TACH
                                                // through a voltage divider - see README. Reads 0 while unwired.
    const u8 FAN_PULSES_PER_REVOLUTION = 2;     // 4-wire fans output 2 pulses per revolution
    const u16 FAN_RPM_UPDATE_INTERVAL = 500;    // ms between RPM measurements
    const u16 FAN_RPM_RATED = 6000;             // RPM your fan is rated for, only used for the HUD readout/logging
    const i32 DEFAULT_PARTICLE_LIFESPAN = 3 SECONDS;

    const u8 DISPLAY_HEIGHT = 8;        // do NOT touch this >:(
}