/*
  VideoSDcard for SmartMatrix Shield V5 and Teensy 4.1
  Plays video stored on an SD card, displaying on a LED matrix using the
  SmartMatrix library.

    Older / copied files comments below:

    OctoWS2811 VideoSDcard.ino - Video on LEDs, played from SD Card
    http://www.pjrc.com/teensy/td_libs_OctoWS2811.html
    Copyright (c) 2014 Paul Stoffregen, PJRC.COM, LLC

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

Update: The programs to prepare the SD card video file have moved to "extras"
https://github.com/PaulStoffregen/OctoWS2811/tree/master/extras

  The recommended hardware for SD card playing is:

    Teensy 3.1:     http://www.pjrc.com/store/teensy31.html
    Octo28 Apaptor: http://www.pjrc.com/store/octo28_adaptor.html
    SD Adaptor:     http://www.pjrc.com/store/wiz820_sd_adaptor.html
    Long Pins:      http://www.digikey.com/product-search/en?keywords=S1082E-36-ND

  See the included "hardware.jpg" image for suggested pin connections,
  with 2 cuts and 1 solder bridge needed for the SD card pin 3 chip select.

  Required Connections
  --------------------
    pin 2:  LED Strip #1    OctoWS2811 drives 8 LED Strips.
    pin 14: LED strip #2    All 8 are the same length.
    pin 7:  LED strip #3
    pin 8:  LED strip #4    A 100 to 220 ohm resistor should used
    pin 6:  LED strip #5    between each Teensy pin and the
    pin 20: LED strip #6    wire to the LED strip, to minimize
    pin 21: LED strip #7    high frequency ringining & noise.
    pin 5:  LED strip #8
    pin 15 & 16 - Connect together, but do not use
    pin 4:  Do not use

    pin 3:  SD Card, CS
    pin 11: SD Card, MOSI
    pin 12: SD Card, MISO
    pin 13: SD Card, SCLK

    10/2/2023: changeset by Marten Silbiger
    github.com/dramamine
    marten@metal-heart.org

    This has been updated in the following ways:
    - Works with Teensy 4.1 and the OctoWS2811 adaptor
    - Audio playback removed
    - Header changed. It's still 5 bytes but the format is the following:
    [0]: should be "*" or 0x2A to designate the start of an image frame
         0x7E to designate the end of the movie file
    [1-2]: the image size. this should match LED_WIDTH * LED_HEIGHT.
           ex. [0x02 0x00] = 2*256+0 = 512 LEDs per frame, or 64 width * 8
           ex. [0x05 0x50] = 5*256 + 80 = 1360 LEDs per frame, or 170 width * 8
    These are just used to validate frames and frame size. Make sure they are set
    correctly below in the #define block.

    [3-4]: framerate, expressed in microseconds per frame.
           ex. [0x82 0x35] = 33333 usec = 30.0 fps
           ex. [0x27 0x10] = 10000 usec = 100.0 fps
           ex. [0x10 0x46] =  4166 usec = 240.0 fps

*/
#include <MatrixHardware_Teensy4_ShieldV5.h>        // SmartLED Shield for Teensy 4 (V5)
#include <SmartMatrix.h>
#include <OctoWS2811.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>

// #define LED_WIDTH    170   // number of LEDs horizontally
// #define LED_HEIGHT   8   // number of LEDs vertically (must be multiple of 8)

#define FILENAME     "output.bin"

// const int ledsPerStrip = LED_WIDTH * LED_HEIGHT / 8;
// DMAMEM int displayMemory[ledsPerStrip*6];
// int drawingMemory[ledsPerStrip*6];
// elapsedMicros elapsedSinceLastFrame = 0;
// bool playing = false;

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);

// @TODO is this right?
int drawingMemory[(kMatrixWidth * kMatrixHeight / 8) * 6]; // 1 byte per pixel for 24 bit color depth

SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

bool playing = false;
elapsedMicros elapsedSinceLastFrame = 0;

// const int config = WS2811_GRB | WS2811_800kHz;
// OctoWS2811 leds(ledsPerStrip, displayMemory, drawingMemory, config);
File videofile;

void setup() {
  delay(50);
  Serial.println("VideoSDcard");
  if (!SD.begin(BUILTIN_SDCARD)) stopWithErrorMessage("Could not access SD card");
  Serial.println("SD card ok");
  videofile = SD.open(FILENAME, FILE_READ);
  if (!videofile) stopWithErrorMessage("Could not read " FILENAME);
  Serial.println("File opened");
  playing = true;
  elapsedSinceLastFrame = 0;

  matrix.addLayer(&backgroundLayer);
  matrix.begin();

  matrix.setBrightness(128);

  // if(led >= 0)  pinMode(led, OUTPUT);

}

// read from the SD card, true=ok, false=unable to read
// the SD library is much faster if all reads are 512 bytes
// this function lets us easily read any size, but always
// requests data from the SD library in 512 byte blocks.
//
bool sd_card_read(void *ptr, unsigned int len)
{
  static unsigned char buffer[512];
  static unsigned int bufpos = 0;
  static unsigned int buflen = 0;
  unsigned char *dest = (unsigned char *)ptr;
  unsigned int n;

  while (len > 0) {
    if (buflen == 0) {
      n = videofile.read(buffer, 512);

      if (n == 0) return false;
      buflen = n;
      bufpos = 0;
    }
    unsigned int n = buflen;
    if (n > len) n = len;
    memcpy(dest, buffer + bufpos, n);
    dest += n;
    bufpos += n;
    buflen -= n;
    len -= n;
  }
  return true;
}

// skip past data from the SD card
void sd_card_skip(unsigned int len)
{
  unsigned char buf[256];

  while (len > 0) {
    unsigned int n = len;
    if (n > sizeof(buf)) n = sizeof(buf);
    sd_card_read(buf, n);
    len -= n;
  }
}

void writeToMatrix() {
  // this function writes from drawingMemory to the matrix background layer
  // assumes drawingMemory is in RGB format, 3 bytes per pixel
  // and that the size matches the matrix dimensions

  uint16_t index = 0;
  for (uint16_t y = 0; y < kMatrixHeight; y++) {
    for (uint16_t x = 0; x < kMatrixWidth; x++) {
      if (index + 2 < sizeof(drawingMemory)) {
        uint8_t r = ((uint8_t *)drawingMemory)[index++];
        uint8_t g = ((uint8_t *)drawingMemory)[index++];
        uint8_t b = ((uint8_t *)drawingMemory)[index++];
        backgroundLayer.drawPixel(x, y, rgb24(r, g, b));
      }
    }
  }
}


void loop()
{
  unsigned char header[5];

  if (playing) {
    if (sd_card_read(header, 5)) {
      // Serial.printf("my header: %u %u %u %u %u\n", header[0], header[1], header[2], header[3], header[4]);
      // Serial.printf("my header: %02X %02X %02X %02X %02X\n", header[0], header[1], header[2], header[3], header[4]);
      if (header[0] == '*') {
        // found an image frame
        unsigned int size = (header[1] | (header[2] << 8)) * 3;
        // note that we could just use LED_WIDTH and LED_HEIGHT here, but this
        // will tell us about read errors when the data encoded doesn't match
        // the constants in this file.
        // unsigned int size = 3*LED_WIDTH*LED_HEIGHT;

        unsigned int usec = header[3] | (header[4] << 8);
        // same deal, we could hardcode usec here but might as well save it in the heade
        // unsigned int usec = 33333; // 30.0 fps
        // unsigned int usec = 25000; // 40.0 fps
        // unsigned int usec = 12500; // 80.0 fps
        // unsigned int usec = 10000; // 100 fps
        // unsigned int usec = 4166; // 240 fps

        // WARNING: using +5 here to fix the offset, but I'm not sure why it's wrong.
        unsigned int readsize = size+5;

        // Serial.printf("size and usec: %u pixels per frame %u\n", size, usec);
        if (readsize > sizeof(drawingMemory)) {
          readsize = sizeof(drawingMemory);
        }
        if (sd_card_read(drawingMemory, readsize)) {
          // Serial.printf(", us = %u", (unsigned int)elapsedSinceLastFrame);
          // Serial.println();
          while (elapsedSinceLastFrame < usec) ; // wait
          elapsedSinceLastFrame -= usec;

          // @new write from drawingMemory to the matrix.
          writeToMatrix();
          backgroundLayer.swapBuffers();

          // leds.show();
          // exit(1);
        } else {
          error("unable to read video frame data");
          return;
        }
        if (readsize < size) {
          sd_card_skip(size - readsize);
        }
      } else if (header[0] == 0x7E) {
        Serial.println("end-of-file detected.");
        return;
      } else {
        error("unknown header");
        delay(2000);
        return;
      }
    } else {
      error("unable to read 5-byte header");
      return;
    }
  } else {
    delay(2000);
    videofile = SD.open(FILENAME, FILE_READ);
    if (videofile) {
      Serial.println("File opened");
      playing = true;
      elapsedSinceLastFrame = 0;
    }
  }
}

// when any error happens during playback, close the file and restart
void error(const char *str)
{
  Serial.print("error: ");
  Serial.println(str);
  videofile.close();
  playing = false;
}

// when an error happens during setup, give up and print a message
// to the serial monitor.
void stopWithErrorMessage(const char *str)
{
  while (1) {
    Serial.println(str);
    delay(1000);
  }
}
