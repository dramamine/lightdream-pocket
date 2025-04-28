/*
Accept Artnet data and display it, through an OctoWS2811 / Teensy / Wiz850io

Install Teensyduino and set board to "Teensy 4.1"

10/13: made this more flexible/generic and added the ability to send multiple
universes per data channel. From testing I think you can send 4 universes per
channel for a combined 32 universes of LED data.

9/12: fixed the 34=>35 conversion bug that I found at the campsite
added constellations but haven't tested or made fancy yet

from 7/23: updated the Artnet library to use NativeEthernet and NativeEthernetUdp
it "just works" after that. tried to use #define TEENSY41 to conditionally load
those specific libraries but that wasn't working for me, was still trying to load
the normal Ethernat library.
After warming up, this was getting 40 fps with 3 universes.

The MIT License (MIT)

Copyright (c) 2018-2023 Marten Silbiger
https://github.com/dramamine/lightdream-scripts

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
#include <FastLED.h>

// i.e. LEDs per output.
#define LED_WIDTH 300

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 1

// Send fps timing to Serial out, should be around 40 fps
bool showFps = false;

// ~~ end config ~~

const int numLeds = LED_WIDTH * LED_HEIGHT;
DMAMEM int displayMemory[LED_WIDTH * 6];
int drawingMemory[LED_WIDTH * 6];
const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LED_WIDTH, displayMemory, drawingMemory, config);

// Artnet settings
Artnet artnet;

// FastLED template

template <EOrder RGB_ORDER = RGB,
          uint8_t CHIP = WS2811_800kHz>
class CTeensy4Controller : public CPixelLEDController<RGB_ORDER, 8, 0xFF>
{
    OctoWS2811 *pocto;

public:
    CTeensy4Controller(OctoWS2811 *_pocto)
        : pocto(_pocto){};

    virtual void init() {}
    virtual void showPixels(PixelController<RGB_ORDER, 8, 0xFF> &pixels)
    {

        uint32_t i = 0;
        while (pixels.has(1))
        {
            uint8_t r = pixels.loadAndScale0();
            uint8_t g = pixels.loadAndScale1();
            uint8_t b = pixels.loadAndScale2();
            pocto->setPixel(i++, r, g, b);
            pixels.stepDithering();
            pixels.advanceData();
        }

        pocto->show();
    }
};

CRGB rgbarray[numLeds];
CTeensy4Controller<GRB, WS2811_800kHz> *pcontroller;

namespace Pattern {
  const int BRIGHTNESS = 200; // out of 255

  void setup() {
    pcontroller = new CTeensy4Controller<GRB, WS2811_800kHz>(&leds);

    FastLED.setBrightness(BRIGHTNESS);
    FastLED.addLeds(pcontroller, rgbarray, numLeds);
    }

  void _twinkle() {
    int i = random16(numLeds);                                           // A random number. Higher number => fewer twinkles. Use random16() for values >255.
    if (i < numLeds) rgbarray[i] = CHSV(random(255), random(255), random(255));              // Only the lowest probability twinkles will do. You could even randomize the hue/saturation. .
    for (int j = 0; j < numLeds; j++) rgbarray[j].fadeToBlackBy(8);

    FastLED.show();                                                // Standard FastLED display
    Serial.println("showing...");
    //show_at_max_brightness_for_power();                          // Power managed FastLED display

    //delay(10);                                            // Standard delay
    FastLED.delay(10);                                     // FastLED delay
    //delay_at_max_brightness_for_power(thisdelay);              // Power managed FastLED delay
  }

  void loop()
  {
    _twinkle();
  }

  void enqueue(int r, int g, int b) {
    // first, move the current pixels to the next pixel in the array
    for (int i = numLeds - 1; i > 0; i--) {
      rgbarray[i] = rgbarray[i - 1];
    }
    // set the first pixel to the new color
    rgbarray[0] = CRGB(r, g, b);
    FastLED.show();
  }
}

namespace Networking {
    // https://www.arduino.cc/reference/en/libraries/ethernet/
  void setup()
  {
    Serial1.begin(9600);   // Hardware Serial1 on pins 0 (TX) and 1 (RX)
    Serial.println("Receiver ready.");
  }

  void loop() {
    if (Serial1.available()) {
      String incoming = Serial1.readStringUntil('\n');
      //Serial.print("Received: ");
      //Serial.println(incoming);
      // incoming should be in the format "r,g,b"
      // parse into r g b values
      int r = incoming.substring(0, incoming.indexOf(',')).toInt();
      int g = incoming.substring(incoming.indexOf(',') + 1, incoming.lastIndexOf(',')).toInt();
      int b = incoming.substring(incoming.lastIndexOf(',') + 1).toInt();
      Pattern::enqueue(r, g, b);
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("INFO:   Serial visualizer. Version: 2025.04");
  Serial.printf("INFO:   LED counter: %d pixels, %d LEDs \n", leds.numPixels(), numLeds);
  Serial.println();

  leds.begin();

  Networking::setup();

  Pattern::setup();
}



void loop()
{
  Networking::loop();
}
