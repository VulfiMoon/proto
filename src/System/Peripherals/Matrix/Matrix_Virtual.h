#pragma once

#include <Arduino.h>
#include "Matrix.h"
#include "config.h"

/**
 * Virtual matrix backend used when testing Toaster Blaster without the
 * physical MAX7219 chain. It keeps exactly the same 128-column framebuffer
 * that Matrix_Max7219 would receive.
 *
 * Column 0 is the first column addressed by the firmware (the far end of the
 * MAX7219 chain), and column 127 is the last.
 */
class Matrix_Virtual : public Matrix {
  private:
    static constexpr size_t NUM_COLUMNS = Config::Displays::NUM_DISPLAYS * 8;
    u8 frame[NUM_COLUMNS]{};
    bool enabled = true;

  public:
    Matrix_Virtual(u8 brightness = 0) : Matrix(brightness) {
        clear();
    }

    void init() override {
        clear();
    }

    void clear() override {
        memset(frame, 0, sizeof(frame));
    }

    void enable(bool value) override {
        enabled = value;
    }

    void setBrightness(u8 value) override {
        brightness = value & 0x0f;
    }

    void drawColumn(u8 col, u8 value) override {
        if (col < NUM_COLUMNS) frame[col] = enabled ? value : 0;
    }

    void drawBitmap(const u8* bitmap, size_t size, u8 position) override {
        for (size_t i = 0; i < size; ++i) {
            const size_t col = (size_t)position + i;
            if (col < NUM_COLUMNS) frame[col] = enabled ? bitmap[i] : 0;
        }
    }

    const u8* getFrame() const {
        return frame;
    }

    size_t getFrameSize() const {
        return NUM_COLUMNS;
    }

    bool isEnabled() const {
        return enabled;
    }
};
