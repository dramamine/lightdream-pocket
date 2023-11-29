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

#include <OctoWS2811.h>

// i.e. LEDs per output.
#define LED_WIDTH 600

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 8

// if true, program expects to be plugged into a network switch. If it's not,
// it will get stuck at `setup()::artnet.begin()`.
// ## Troubleshooting the network
// If you see "Link status (should be 2)"
bool useNetwork = false;

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

// how many universes per strip?
const int universesPerStrip = ceil(LED_WIDTH / 170.0);

const int maxUniverses = LED_HEIGHT * universesPerStrip;

const int numLeds = LED_WIDTH * LED_HEIGHT;
DMAMEM int displayMemory[LED_WIDTH * 6];
int drawingMemory[LED_WIDTH * 6];
const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LED_WIDTH, displayMemory, drawingMemory, config);


namespace Pattern {

  const int fps = 11;
  const int BRIGHTNESS = 50;
  int ticks = 0;

  long hues[256];

  long setLedColorHSV(byte h, byte s, byte v)
  {
    byte RedLight;
    byte GreenLight;
    byte BlueLight;
    // this is the algorithm to convert from RGB to HSV
    h = (h * 192) / 256;           // 0..191
    unsigned int i = h / 32;       // We want a value of 0 thru 5
    unsigned int f = (h % 32) * 8; // 'fractional' part of 'i' 0..248 in jumps

    unsigned int sInv = 255 - s; // 0 -> 0xff, 0xff -> 0
    unsigned int fInv = 255 - f; // 0 -> 0xff, 0xff -> 0
    byte pv = v * sInv / 256;    // pv will be in range 0 - 255
    byte qv = v * (256 - s * f / 256) / 256;
    byte tv = v * (256 - s * fInv / 256) / 256;

    switch (i)
    {
    case 0:
      RedLight = v;
      GreenLight = tv;
      BlueLight = pv;
      break;
    case 1:
      RedLight = qv;
      GreenLight = v;
      BlueLight = pv;
      break;
    case 2:
      RedLight = pv;
      GreenLight = v;
      BlueLight = tv;
      break;
    case 3:
      RedLight = pv;
      GreenLight = qv;
      BlueLight = v;
      break;
    case 4:
      RedLight = tv;
      GreenLight = pv;
      BlueLight = v;
      break;
    case 5:
      RedLight = v;
      GreenLight = pv;
      BlueLight = qv;
      break;
    }
    long rgb = 0;

    rgb += RedLight << 16;
    rgb += GreenLight << 8;
    rgb += BlueLight;
    return rgb;
  }

  void rainbowSetup()
  {
    for (int i = 0; i < 256; i++)
    {
      hues[i] = setLedColorHSV(i, 255, BRIGHTNESS);
    }
  }

  void setup() {
    rainbowSetup();
  }

  void loop()
  {
    for (uint16_t i=0; i<numLeds; i++) {
      // strip number, plus one
      int sn = floor(i / LED_WIDTH) + 2;
      if (i % sn == 0) {
        leds.setPixelColor(i, 0,0,0);
      } else {
        leds.setPixelColor(i, hues[(i+ticks) % 256]);
      }
    }
    leds.show();
    ticks++;
    delay((int)1000/fps);
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("INFO:   Version: 2023.11");
  Serial.printf("INFO:   LED counter: %d pixels, %d LEDs \n", leds.numPixels(), numLeds);
  Serial.println();

  leds.begin();

  Pattern::setup();
}

void loop()
{
  Pattern::loop();
}
