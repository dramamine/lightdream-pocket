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

Copyright (c) 2014 Nathanaël Lécaudé
https://github.com/natcl/Artnet, http://forum.pjrc.com/threads/24688-Artnet-to-OctoWS2811

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
// for the artnet library to load the right ethernet stuff.
// due to edits I made, it now loads NativeEthernet and NativeEthernetUDP
#include "Artnet.h"
#include <SPI.h>
#include <OctoWS2811.h>
#include "TeensyID.h"
#include <MatrixHardware_Teensy4_ShieldV5.h>        // SmartLED Shield for Teensy 4 (V5)
#include <SmartMatrix.h>
#include <Wire.h>

#define version "2025.10"

// if true, program expects to be plugged into a network switch. If it's not,
// it will get stuck at `setup()::artnet.begin()`.
// ## Troubleshooting the network
// If you see "Link status (should be 2)"
bool useNetwork = true;

// make sure the config above is correct for your setup. we expect the controlling
// software  to send (LED_HEIGHT * universesPerStrip) universes to this IP.
const int ledsPerUniverse = 170;

// Send fps timing to Serial out, should be around 40 fps
bool showFps = true;

// how long is our update look taking to render?
// for reference: runs about 12us for regular, 32-universe code
// LD algorithm Q3-2023 was running 15-17us for 8-universe code
bool showTiming = false;


// ~~ end config ~~

#define COLOR_DEPTH 24                  // Choose the color depth used for storing pixels in the layers: 24 or 48 (24 is good for most sketches - If the sketch uses type `rgb24` directly, COLOR_DEPTH must be 24)
const uint16_t kMatrixWidth = 64;       // Set to the width of your display, must be a multiple of 8
const uint16_t kMatrixHeight = 64*3;      // Set to the height of your display
const uint8_t kRefreshDepth = 36;       // Tradeoff of color quality vs refresh rate, max brightness, and RAM usage.  36 is typically good, drop down to 24 if you need to.  On Teensy, multiples of 3, up to 48: 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, 33, 36, 39, 42, 45, 48.  On ESP32: 24, 36, 48
const uint8_t kDmaBufferRows = 4;       // known working: 2-4, use 2 to save RAM, more to keep from dropping frames and automatically lowering refresh rate.  (This isn't used on ESP32, leave as default)
const uint8_t kPanelType = SM_PANELTYPE_HUB75_32ROW_MOD16SCAN;   // Choose the configuration that matches your panels.  See more details in MatrixCommonHub75.h and the docs: https://github.com/pixelmatix/SmartMatrix/wiki
const uint32_t kMatrixOptions = (SM_HUB75_OPTIONS_NONE);        // see docs for options: https://github.com/pixelmatix/SmartMatrix/wiki
const uint8_t kBackgroundLayerOptions = (SM_BACKGROUND_OPTIONS_NONE);

const int maxUniverses = 73; // set later
const int numLeds = kMatrixWidth * kMatrixHeight;

// @TODO is this right?
int drawingMemory[(kMatrixWidth * kMatrixHeight / 8) * 6]; // 1 byte per pixel for 24 bit color depth


SMARTMATRIX_ALLOCATE_BUFFERS(matrix, kMatrixWidth, kMatrixHeight, kRefreshDepth, kDmaBufferRows, kPanelType, kMatrixOptions);
SMARTMATRIX_ALLOCATE_BACKGROUND_LAYER(backgroundLayer, kMatrixWidth, kMatrixHeight, COLOR_DEPTH, kBackgroundLayerOptions);

// Artnet settings
Artnet artnet;

byte timeOffset = 0;

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

namespace Networking {

  // Change ip for your setup, last octet is changed in updateIp()
  byte _ip[] = {169, 254, 18, 0};
  byte _fakemac[] = {0x04, 0xE9, 0xE5, 0x00, 0x69, 0xEC};

  // have we received data for each universe?
  bool universesReceived[maxUniverses];

  // for calculating data received rates
  int universesReceivedTotal[maxUniverses];
  bool sendFrame = 1;

  // true once we have received an Artnet packet
  bool hasReceivedArtnetPacket = false;

  // frame time in ms, using millis()
  uint32_t _frameMs = 0;


  void updateLeds(int uni) {
    uint8_t *frame = artnet.getDmxFrame();
    int length = artnet.getLength();
    for (int i = 0; i < length / 3; i++)
    {
      int led = i + uni * ledsPerUniverse;
      if (led < numLeds)
      {
        // @TODO is this function right? pretty hard to tell.
        // consider using memcpy
        // Combine RGB into a single 24-bit value and store in drawingMemory
        ((uint8_t*)drawingMemory)[led * 3]     = frame[i * 3];
        ((uint8_t*)drawingMemory)[led * 3 + 1] = frame[i * 3 + 1];
        ((uint8_t*)drawingMemory)[led * 3 + 2] = frame[i * 3 + 2];
        // Alternatively, if drawingMemory is uint32_t[] and you want to store as 0xRRGGBB:
        // drawingMemory[led] = (frame[i * 3] << 16) | (frame[i * 3 + 1] << 8) | frame[i * 3 + 2];
        // leds.setPixel(led, frame[i * 3], frame[i * 3 + 1], frame[i * 3 + 2]);
      }
    }
  }

  // void updateLeds(int uni) {
  //   uint8_t *frame = artnet.getDmxFrame();
  //   int length = artnet.getLength();
  //   for (int i = 0; i < length / 3; i++)
  //   {
  //     int led = i + uni * ledsPerUniverse;
  //     if (led < numLeds)
  //     {
  //       // @TODO is this function right? pretty hard to tell.
  //       // consider using memcpy
  //       // Combine RGB into a single 24-bit value and store in drawingMemory
  //       ((uint8_t*)drawingMemory)[led * 3]     = frame[i * 3];
  //       ((uint8_t*)drawingMemory)[led * 3 + 1] = frame[i * 3 + 1];
  //       ((uint8_t*)drawingMemory)[led * 3 + 2] = frame[i * 3 + 2];
  //       // Alternatively, if drawingMemory is uint32_t[] and you want to store as 0xRRGGBB:
  //       // drawingMemory[led] = (frame[i * 3] << 16) | (frame[i * 3 + 1] << 8) | frame[i * 3 + 2];
  //       // leds.setPixel(led, frame[i * 3], frame[i * 3 + 1], frame[i * 3 + 2]);
  //     }
  //   }
  // }


  // https://www.arduino.cc/reference/en/libraries/ethernet/
  void setup()
  {
    // Networking::updateIp();

    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("ERROR:  Ethernet shield was not found.");
    }
    else if (Ethernet.hardwareStatus() == EthernetW5100) {
      Serial.println("INFO:  W5100 Ethernet controller detected.");
    }
    else if (Ethernet.hardwareStatus() == EthernetW5200) {
      Serial.println("INFO:   W5200 Ethernet controller detected.");
    }
    else if (Ethernet.hardwareStatus() == EthernetW5500) {
      Serial.println("INFO:   W5500 Ethernet controller detected.");
    }

    Serial.println("INFO:   Setting up Artnet via Ethernet cable...");
    Serial.printf("INFO:   Link status (should be 2): %d\n", Ethernet.linkStatus());
    if (Ethernet.linkStatus() != 2) {
      Serial.println("ERROR:  Something wrong with link status. Make sure your Ethernet kit is installed properly.");
      Serial.println("ERROR:  https://www.pjrc.com/store/ethernet_kit.html");
      Serial.println("ERROR:  Turning networking requirement off.");
      useNetwork = false;
      return;
    }

    Serial.println("STATUS: Connected to network switch.");
    artnet.begin(_fakemac, _ip);

    Serial.println("STATUS: Listening for Artnet data.");
    Serial.print("INFO:   Local ip: ");
    Serial.println(Ethernet.localIP());
  }


  // print fps and how many frames we've received from each universe. this
  // prints incrementally (every 100 frames, when universe 0 is received)
  void printFps() {
    int uni = artnet.getUniverse();
    if (uni == 0 && universesReceivedTotal[0] % 100 == 0) {
      // check timing, do fps
      uint32_t currentTiming = millis();
      if (_frameMs > 0)
      {
        float fps = 100000. / (currentTiming - _frameMs);
        Serial.printf("PERF:   %2.2f fps.  ", fps);
      }
      _frameMs = currentTiming;

      // print how many frames we got from each universe
      for (int i = 0; i < maxUniverses; i++)
      {
        Serial.print(i);
        Serial.print(": ");
        //float pct = 100 * universesReceivedTotal[i] / universesReceivedTotal[0];
        float pct = universesReceivedTotal[i];
        Serial.print(pct, 2);
        Serial.print(" ");
      }
      Serial.print("\n");
    }
  }

  void handleDmxFrame()
  {
    int uni = artnet.getUniverse();
    // Serial.println(uni);

    if (uni >= maxUniverses) {
      return;
    }

    // tracking
    universesReceived[uni] = 1;
    universesReceivedTotal[uni] = universesReceivedTotal[uni] + 1;

    if (showFps) {
      Networking::printFps();
    }

    // flash LED along with received data
    if (uni == 0 && universesReceivedTotal[0] % 30 == 0) {
      if (uni == 0 && universesReceivedTotal[0] % 60 == 0) {
        digitalWrite(LED_BUILTIN, HIGH);
      } else {
        digitalWrite(LED_BUILTIN, LOW);
      }
    }

    // how many microseconds to perform these operations for one Artnet frame?
    if (showTiming) {
      uint32_t beginTime = micros();
      updateLeds(uni);
      uint32_t elapsedTime = micros() - beginTime;
      Serial.printf("PERF:   elapsed microseconds: %lu \n", elapsedTime);
    } else {
      updateLeds(uni);
    }

    // if we've received data for each universe, call leds.show()

    sendFrame = 1;
    for (int i = 0; i < maxUniverses; i++)
    {
      if (universesReceived[i] == 0)
      {
        // Serial.printf("sendFrame is 0 on universe: %d (of %d)\n", i, maxUniverses);
        sendFrame = 0;
        break;
      }
    }

    if (sendFrame)
    {
      writeToMatrix();
      backgroundLayer.swapBuffers();
      memset(universesReceived, 0, maxUniverses);
    }
  }
  void loop() {
    if (useNetwork) {
      uint16_t r = artnet.read();
      if (r == ART_DMX) {
        // system state update
        if (!Networking::hasReceivedArtnetPacket)
        {
          Serial.println("STATUS: Receiving Artnet data.");
          Networking::hasReceivedArtnetPacket = true;
          // black out each LED
          // for (int i = 0; i < numLeds; i++)
          // {
          //   leds.setPixel(i, 0, 0, 0);
          // }
          // leds.show();
        }

        Networking::handleDmxFrame();
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
  Serial.println();

  // Initialize SmartMatrix
  matrix.addLayer(&backgroundLayer);
  matrix.begin();

  // Start background layer
  backgroundLayer.enableColorCorrection(false);
  backgroundLayer.fillScreen(rgb24(0, 0, 0)); // Clear to black
  backgroundLayer.swapBuffers();

  Serial.println("SmartMatrix initialized");

  // @TODO see if we can handle failure in a cleaner way here
  if (useNetwork) {
    Networking::setup();
  }
}



void loop()
{
  if (useNetwork) {
    Networking::loop();
  }


  // if (!Networking::hasReceivedArtnetPacket)
  // {
  //   Pattern::loop();
  // }
}
