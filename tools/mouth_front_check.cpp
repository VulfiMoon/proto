/**
 * Host-side (PC) check for the front mouth panel stitching.
 *
 * This is NOT part of the firmware - it is a tiny standalone program that
 * reproduces the buffer layout of Display/DisplayManager and the global
 * LambdaEffect that builds MOUTH_FRONT out of MOUTH_L / MOUTH_R, so the
 * result can be inspected as ASCII art without any hardware.
 *
 * Build & run:
 *     g++ -std=c++17 -Wall tools/mouth_front_check.cpp -o /tmp/mouthcheck && /tmp/mouthcheck
 *
 * Buffer layout (see System/Utilities/Util.cpp, pointToMatrix):
 *     pixel (x, y)  ->  buffer[y + (x / 8) * 8], bit (7 - x % 8)
 * so one buffer byte is one *row* of one 8x8 module, and its bits are the 8
 * columns of that module. Eight consecutive bytes = one 8x8 block of the image.
 */

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using u8 = uint8_t;

using f32 = float;

static const u8 DISPLAY_HEIGHT = 8;

// --- buffer helpers (mirror of Util.cpp / ExpandY.h) ------------------------
static inline void setPixelRaw(u8* buffer, size_t size, u8 x, u8 y, bool on) {
    size_t index = y + (x / 8) * 8;
    u8 bit = 7 - (x % 8);
    if(index >= size) return;
    if(on) buffer[index] |= (1 << bit);
    else   buffer[index] &= ~(1 << bit);
}

static inline bool getPixelRaw(const u8* buffer, size_t size, u8 x, u8 y) {
    size_t index = y + (x / 8) * 8;
    u8 bit = 7 - (x % 8);
    if(index >= size) return false;
    return (buffer[index] >> bit) & 1;
}

static inline u8 mirrorInt(u8 number) {
    u8 result = 0;
    for(u8 i = 0; i < 8; i++) {
        result = (result << 1) | (number & 1);
        number >>= 1;
    }
    return result;
}

// Mirror effect, as in System/Drawing/Effects/Mirror.h (size 32 -> 4 blocks)
static void mirror(u8* buffer, size_t size) {
    for(u8 i = 0; i < size / 16; i++) {
        for(u8 j = 0; j < 8; j++) {
            u8 temp = mirrorInt(buffer[i * 8 + j]);
            buffer[i * 8 + j] = mirrorInt(buffer[size - (i + 1) * 8 + j]);
            buffer[size - (i + 1) * 8 + j] = temp;
        }
    }
}

// ExpandY::expandY, as in System/Drawing/Effects/ExpandY.h
static void expandY(u8* buffer, size_t size, u8 column, u8 amount) {
    if(amount == 0) return;
    bool pixels[DISPLAY_HEIGHT];
    for(u8 i = 0; i < DISPLAY_HEIGHT; i++) pixels[i] = getPixelRaw(buffer, size, column, i);

    u8 above = amount / 2;
    u8 below = amount - above;
    for(u8 i = 0; i < DISPLAY_HEIGHT; i++) {
        if(!pixels[i]) continue;
        for(u8 j = 1; j <= above; j++) {
            if(i - j < 0) break;
            setPixelRaw(buffer, size, column, i - j, true);
        }
        for(u8 j = 1; j <= below; j++) {
            if(i + j > 7) break;
            setPixelRaw(buffer, size, column, i + j, true);
        }
    }
}

// MouthEffect, as in System/Drawing/Effects/MouthEffect.h
static void mouthEffect(u8* buffer, size_t size, const u8* values, bool direction) {
    for(u8 i = 0; i < size; i++) {
        if(direction) expandY(buffer, size, i, values[i]);
        else          expandY(buffer, size, size - i - 1, values[i]);
    }
}

// --- synthetic "smile" art, 32x8, one pixel tall line ----------------------
static void makeMouthArt(u8* buffer, size_t size) {
    memset(buffer, 0, size);
    for(u8 x = 0; x < size; x++) {
        // gentle curve: high at the front of the muzzle (x = 31), dip in the middle
        u8 y = (u8)(1 + 5.0f * (1.0f - x / (f32)(size - 1)));
        if(y > 7) y = 7;
        setPixelRaw(buffer, size, x, y, true);
    }
}

static void printDisplay(const char* title, const u8* buffer, size_t size) {
    printf("%s\n", title);
    for(u8 y = 0; y < DISPLAY_HEIGHT; y++) {
        printf("    ");
        for(size_t x = 0; x < size; x++) printf("%c", getPixelRaw(buffer, size, x, y) ? '#' : '.');
        printf("\n");
    }
    printf("\n");
}

// Prints the whole front of the muzzle the way it is wired:
// MOUTH_L (back->front) | NOSE_L | FRONT | NOSE_R | MOUTH_R (front->back)
static void printFace(const char* title, const u8* mouthL, const u8* mouthR, const u8* front) {
    printf("%s\n", title);
    for(u8 y = 0; y < DISPLAY_HEIGHT; y++) {
        printf("    ");
        for(int x = 31; x >= 0; x--)  printf("%c", getPixelRaw(mouthL, 32, x, y) ? '#' : '.');  // physical: image 31 = back ... 0 = nose
        printf("|");
        for(u8 x = 0; x < 8; x++)     printf(" ");                                              // NOSE_L (no art in this check)
        printf("|");
        for(int x = 15; x >= 0; x--)  printf("%c", getPixelRaw(front, 16, x, y) ? '#' : '.');    // physical: image 15 = left ... 0 = right
        printf("|");
        for(u8 x = 0; x < 8; x++)     printf(" ");                                              // NOSE_R
        printf("|");
        for(int x = 31; x >= 0; x--)  printf("%c", getPixelRaw(mouthR, 32, x, y) ? '#' : '.');  // physical: image 0 = back ... 31 = nose
        printf("\n");
    }
    printf("\n");
}

// OLD behaviour, straight out of the previous main.cpp
static void stitchOld(const u8* mouthL, const u8* mouthR, u8* front) {
    for(u8 k = 0; k < 8; k++) front[k] = mouthR[k];
    for(u8 k = 0; k < 8; k++) front[15 - k] = mouthL[31 - k];
}

// NEW behaviour: take the blocks that physically border the noses and mirror them
// into the panel, image column order preserved (byte index = image row).
static void stitchNew(const u8* mouthL, const u8* mouthR, u8* front, u8 blockL, u8 blockR, bool mirrorHalves) {
    for(u8 k = 0; k < 8; k++) {
        u8 right = mouthR[blockR * 8 + k];
        u8 left  = mouthL[blockL * 8 + k];
        if(mirrorHalves) {
            right = mirrorInt(right);
            left  = mirrorInt(left);
        }
        front[k] = right;        // right half of the panel (borders NOSE_R)
        front[8 + k] = left;     // left half of the panel (borders NOSE_L)
    }
}

int main() {
    u8 art[32];
    makeMouthArt(art, 32);
    printDisplay("mouth art (image space, x = 0..31, image column 31 = nose end of MOUTH_R):", art, 32);

    // bar heights as produced by AnimatedMouth::update() for a loud reading
    u8 barsQuiet[32], barsLoud[32];
    const f32 normalizedQuiet = 0.0f;
    const f32 normalizedLoud  = 0.25f;   // max value AnimatedMouth can produce
    for(u8 i = 0; i < 32; i++) {
        barsQuiet[i] = (u8)(normalizedQuiet * i);
        barsLoud[i]  = (u8)(normalizedLoud  * i);
    }
    printf("bar heights (loud): ");
    for(u8 i = 0; i < 32; i++) printf("%d ", barsLoud[i]);
    printf("\n\n");

    const char* variants[] = {"OLD (current firmware)", "NEW (blocks 3 / 0, mirrored)"};

    for(int v = 0; v < 2; v++) {
        for(int loud = 0; loud < 2; loud++) {
            u8 mouthL[32], mouthR[32], front[16];
            memcpy(mouthR, art, 32);        // both mouths get the same bitmap
            memcpy(mouthL, art, 32);
            mirror(mouthL, 32);             // mirrorLeft effect (LEFT_SIDE)

            mouthEffect(mouthR, 32, loud ? barsLoud : barsQuiet, true);    // MouthEffect(MOUTH_R, bars, true)
            mouthEffect(mouthL, 32, loud ? barsLoud : barsQuiet, false);   // MouthEffect(MOUTH_L, bars)

            memset(front, 0, 16);
            if(v == 0) stitchOld(mouthL, mouthR, front);
            else       stitchNew(mouthL, mouthR, front, 0, 3, true);

            char title[128];
            snprintf(title, sizeof(title), "=== %s, mic %s ===", variants[v], loud ? "LOUD (talking)" : "quiet");
            printFace(title, mouthL, mouthR, front);
        }
    }
    return 0;
}
