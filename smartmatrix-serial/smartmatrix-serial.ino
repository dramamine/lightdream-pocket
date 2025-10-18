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


// ~~ end config ~~

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64*3;      // Set to the height of your display
const uint8_t kRefreshDepth = 24;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
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

  void updateLeds() {
    // Process OPC frame data (skip 4-byte header)
    uint8_t *pixelData = &opcBuffer[4];

    for (int led = 0; led < numLeds; led++) {
      // Calculate x,y coordinates from linear LED index
      uint16_t x = led % kMatrixWidth;
      uint16_t y = led / kMatrixWidth;

      // Read RGB data for this pixel
      uint8_t r = pixelData[led * 3];
      uint8_t g = pixelData[led * 3 + 1];
      uint8_t b = pixelData[led * 3 + 2];

      // Write directly to matrix background layer
      backgroundLayer.drawPixel(x, y, rgb24(r, g, b));
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
