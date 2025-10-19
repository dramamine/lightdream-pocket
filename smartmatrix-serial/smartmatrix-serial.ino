/*
Accept Artnet data and display it, through an OctoWS2811 / Teensy / Wiz850io

Be sure to set some configuration values below so that universes line up.

In Resolume -> Advanced Output, do Auto Span on the lumiverse.

This uses a very specific Artnet library that I modified to work with
Teensy 4.1. I've had trouble getting other Artnet libraries to work but there
are definitely more recent libraries that might work "better", but this is
doing the trick for me.

Install Teensyduino and set board to "Teensy 4.1"

The MIT License (MIT)

Copyright (c) 2018-2025 Marten Silbiger
https://github.com/dramamine/lightdream-pocket

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

Resources:
https://www.pjrc.com/teensy/td_libs_OctoWS2811.html

*/
#include <SPI.h>
#include <OctoWS2811.h>
#include "TeensyID.h"
#include <MatrixHardware_Teensy4_ShieldV5.h>        // SmartLED Shield for Teensy 4 (V5)
#include <SmartMatrix.h>
#include <Wire.h>

#define version "2025.10"

// ## Troubleshooting the network
// If you see "Link status (should be 2)"
bool useNetwork = true;

// Send fps timing to Serial out, should be around 40 fps
bool showFps = false;

// how long is our update look taking to render?
// for reference: runs about 12us for regular, 32-universe code
// LD algorithm Q3-2023 was running 15-17us for 8-universe code
bool showTiming = false;

// Advanced visual enhancement options
bool enableFrameInterpolation = true;   // Smooth motion between frames
bool enableTemporalDithering = true;    // Better color depth using temporal dithering
bool enableGammaCorrection = false;      // Perceptually correct brightness
float gammaValue = 2.2;                 // Standard gamma correction
bool enableColorBoost = false;           // Enhance color saturation
float colorBoostFactor = 1.2;           // Color saturation multiplier


// ~~ end config ~~

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64*3;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Higher refresh depth for better color quality at high refresh rates. 36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);

const int numLeds = kMatrixWidth * kMatrixHeight;

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

byte timeOffset = 0;

namespace Networking {

  // OPC (Open Pixel Control) protocol variables
  static uint8_t opcBuffer[4 + (numLeds * 3)]; // 4-byte header + pixel data
  static int opcBufferPos = 0;
  static bool opcFrameReady = false;
  static uint32_t frameCount = 0;
  static uint32_t _frameMs = 0;

  // Advanced visual enhancement buffers
  static uint8_t previousFrame[numLeds * 3];    // Previous frame for interpolation
  static uint8_t currentFrame[numLeds * 3];     // Current frame buffer
  static bool hasPreviousFrame = false;
  static uint32_t lastFrameTime = 0;
  static uint32_t frameInterval = 33;           // Expected ms between frames (30fps = 33ms)
  static uint8_t ditherCounter = 0;             // For temporal dithering

  // Gamma correction lookup table (computed once)
  static uint8_t gammaLUT[256];
  static bool gammaLUTInitialized = false;

  void initializeGammaLUT() {
    if (!gammaLUTInitialized) {
      for (int i = 0; i < 256; i++) {
        float normalized = i / 255.0;
        float corrected = pow(normalized, gammaValue);
        gammaLUT[i] = (uint8_t)(corrected * 255);
      }
      gammaLUTInitialized = true;
    }
  }

  uint8_t applyGamma(uint8_t value) {
    return enableGammaCorrection ? gammaLUT[value] : value;
  }

  uint8_t enhanceColor(uint8_t value) {
    if (!enableColorBoost) return value;

    float enhanced = (value / 255.0);
    enhanced = pow(enhanced, 1.0 / colorBoostFactor); // Boost saturation
    enhanced = enhanced * 255.0;

    return (uint8_t)constrain(enhanced, 0, 255);
  }

  uint8_t interpolateValue(uint8_t prev, uint8_t curr, float alpha) {
    return (uint8_t)(prev * (1.0 - alpha) + curr * alpha);
  }

  uint8_t applyTemporalDither(uint8_t value, int pixelIndex) {
    if (!enableTemporalDithering) return value;

    // Keep black pixels black - don't dither pure black
    // if (value == 0) return 0;

    // Keep pure white pixels white - don't dither pure white
    if (value == 255) return 255;

    // Only apply dithering to mid-range values where it helps
    if (value < 4) return value; // Protect very dark pixels (reduced threshold)

    // Enhanced temporal dithering pattern with more cycles
    uint8_t dither = ((pixelIndex * 3 + ditherCounter) & 0x07); // 8-frame cycle for more variation

    // Increase dither strength based on pixel value
    int ditherAmount;
    if (value < 32) {
      // Small dither for dark values
      ditherAmount = (dither >> 1) - 1; // -1 to +2 range
    } else if (value < 128) {
      // Medium dither for mid values
      ditherAmount = dither - 3; // -3 to +4 range
    } else {
      // Stronger dither for bright values
      ditherAmount = (dither - 3) * 1.5; // -4.5 to +6 range (scaled)
    }

    int dithered = value + ditherAmount;
    return (uint8_t)constrain(dithered, 0, 255);
  }

  void updateLeds() {
    // Initialize gamma LUT if needed
    if (enableGammaCorrection && !gammaLUTInitialized) {
      initializeGammaLUT();
    }

    // Process OPC frame data (skip 4-byte header)
    uint8_t *pixelData = &opcBuffer[4];
    uint32_t currentTime = millis();

    // Copy new frame data and apply initial processing
    for (int i = 0; i < numLeds * 3; i++) {
      currentFrame[i] = pixelData[i];
    }

    // Calculate interpolation factor for smooth motion
    float interpAlpha = 1.0;
    if (enableFrameInterpolation && hasPreviousFrame) {
      uint32_t timeSinceLastFrame = currentTime - lastFrameTime;
      if (timeSinceLastFrame < frameInterval * 2) { // Only interpolate if frame timing is reasonable
        interpAlpha = min(1.0, (float)timeSinceLastFrame / frameInterval);
      }
    }

    // Update dither counter for temporal dithering
    ditherCounter = (ditherCounter + 1) & 0xFF;

    // Process each pixel with enhancements
    for (int led = 0; led < numLeds; led++) {
      // Calculate x,y coordinates from linear LED index
      uint16_t x = led % kMatrixWidth;
      uint16_t y = led / kMatrixWidth;

      // Get current pixel RGB
      uint8_t r = currentFrame[led * 3];
      uint8_t g = currentFrame[led * 3 + 1];
      uint8_t b = currentFrame[led * 3 + 2];

      // Apply frame interpolation for smooth motion
      if (enableFrameInterpolation && hasPreviousFrame && interpAlpha < 1.0) {
        r = interpolateValue(previousFrame[led * 3], r, interpAlpha);
        g = interpolateValue(previousFrame[led * 3 + 1], g, interpAlpha);
        b = interpolateValue(previousFrame[led * 3 + 2], b, interpAlpha);
      }

      // Apply color enhancement
      r = enhanceColor(r);
      g = enhanceColor(g);
      b = enhanceColor(b);

      // Apply gamma correction
      r = applyGamma(r);
      g = applyGamma(g);
      b = applyGamma(b);

      // Apply temporal dithering for better color depth
      r = applyTemporalDither(r, led);
      g = applyTemporalDither(g, led);
      b = applyTemporalDither(b, led);

      // Write to matrix background layer
      backgroundLayer.drawPixel(x, y, rgb24(r, g, b));
    }

    // Store current frame as previous frame for next interpolation
    if (enableFrameInterpolation) {
      memcpy(previousFrame, currentFrame, numLeds * 3);
      hasPreviousFrame = true;
      lastFrameTime = currentTime;
    }
  }

  // https://www.arduino.cc/reference/en/libraries/ethernet/
  void setup()
  {
    Serial.println("STATUS: Listening for OPC data on Serial port.");
    opcBufferPos = 0;
    opcFrameReady = false;
  }


  // print fps for OPC frames
  void printFps() {
    if (frameCount % 100 == 0) {
      // check timing, do fps
      uint32_t currentTiming = millis();
      if (_frameMs > 0)
      {
        float fps = 100000. / (currentTiming - _frameMs);
        Serial.printf("PERF:   %2.2f fps, frame count: %lu\n", fps, frameCount);
      }
      _frameMs = currentTiming;
    }
  }

  void loop() {
    // Read OPC (Open Pixel Control) data from Serial
    while (Serial.available() > 0) {
      uint8_t incomingByte = Serial.read();

      // Add byte to buffer
      if (opcBufferPos < sizeof(opcBuffer)) {
        opcBuffer[opcBufferPos++] = incomingByte;

        // Check if we have at least the 4-byte header
        if (opcBufferPos >= 4) {
          // Parse OPC header: [channel][command][length_hi][length_lo]
          uint8_t channel = opcBuffer[0];
          uint8_t command = opcBuffer[1];
          uint16_t length = (opcBuffer[2] << 8) | opcBuffer[3];

          // Expected frame size: header + pixel data
          uint16_t expectedFrameSize = 4 + length;

          // Check if we have a complete frame
          if (opcBufferPos >= expectedFrameSize) {
            // Validate the frame
            if (command == 0 && length == (numLeds * 3)) {
              // Valid OPC frame with correct pixel count
              opcFrameReady = true;
              frameCount++;

              // Flash LED to indicate data received
              if (frameCount % 30 == 0) {
                digitalWrite(LED_BUILTIN, (frameCount % 60 == 0) ? HIGH : LOW);
              }

              // Process the frame
              if (showTiming) {
                uint32_t beginTime = micros();
                updateLeds();
                uint32_t elapsedTime = micros() - beginTime;
                Serial.printf("PERF:   elapsed microseconds: %lu \n", elapsedTime);
              } else {
                updateLeds();
              }

              // Update display
              backgroundLayer.swapBuffers();

              if (showFps) {
                printFps();
              }

              opcFrameReady = false;
            } else {
              Serial.printf("Invalid OPC frame: cmd=%d, length=%d (expected %d)\n",
                           command, length, numLeds * 3);
            }

            // Reset buffer for next frame
            opcBufferPos = 0;
          }
        }
      } else {
        // Buffer overflow - reset and try again
        Serial.println("OPC buffer overflow, resetting");
        opcBufferPos = 0;
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.printf("INFO:   Version: %s\n", version);
  Serial.printf("INFO:   Matrix dimensions: %dx%d pixels \n", kMatrixWidth, kMatrixHeight);
  Serial.printf("INFO:   Expected OPC data size: %d bytes per frame\n", 4 + (numLeds * 3));
  Serial.printf("INFO:   Refresh depth: %d-bit (higher = better color)\n", kRefreshDepth);
  Serial.printf("INFO:   Visual enhancements:\n");
  Serial.printf("        - Frame interpolation: %s\n", enableFrameInterpolation ? "ON" : "OFF");
  Serial.printf("        - Temporal dithering: %s\n", enableTemporalDithering ? "ON" : "OFF");
  Serial.printf("        - Gamma correction: %s (%.1f)\n", enableGammaCorrection ? "ON" : "OFF", gammaValue);
  Serial.printf("        - Color boost: %s (%.1fx)\n", enableColorBoost ? "ON" : "OFF", colorBoostFactor);
  Serial.println();

  // Initialize SmartMatrix
  matrix.addLayer(&backgroundLayer);
  matrix.begin();

  // Start background layer
  backgroundLayer.enableColorCorrection(false);
  backgroundLayer.fillScreen(rgb24(0, 0, 0)); // Clear to black
  backgroundLayer.swapBuffers();

  Serial.println("SmartMatrix initialized");

  Networking::setup();
}



void loop()
{
  Networking::loop();
}
