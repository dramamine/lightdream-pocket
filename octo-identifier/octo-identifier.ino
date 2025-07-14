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

//

# strand 0 (orange): 40? leds   backpack top loop, third towards zipper
# strand 1 (blue):   77  leds   front top area
# strand 2 (green):  40? leds   backpack top loop, second towards zipper
# strand 3 (brown):  33  leds   front bottom area
# strand 4 (orange): 40? leds   backpack top loop, first towards zipper
# strand 5 (blue):   35  leds   front pocket area


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

#include <SPI.h>
#include <OctoWS2811.h>
#include <FastLED.h>

// i.e. LEDs per output.
#define LED_WIDTH 1000

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
bool showFps = false;

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
  const int BRIGHTNESS = 25; // out of 255

  void setup() {
    pcontroller = new CTeensy4Controller<GRB, WS2811_800kHz>(&leds);

    FastLED.setBrightness(BRIGHTNESS);
    FastLED.addLeds(pcontroller, rgbarray, numLeds);
    // rgbarray[5] = CHSV(20, 255, 255);
  }

  void fadeall() { for(int i = 0; i < numLeds; i++) { rgbarray[i].nscale8(250); } }

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


  void _cylon() {
    static uint8_t hue = 0;
    Serial.print("x");
    // First slide the led in one direction
    for(int i = 0; i < numLeds; i++) {
        // Set the i'th led to red
        rgbarray[i] = CHSV(hue++, 255, 255);
        // Show the leds
        FastLED.show();
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        // Wait a little bit before we loop around and do it again
        delay(10);
    }
    Serial.print("x");

    // Now go in the other direction.
    for(int i = (numLeds)-1; i >= 0; i--) {
        // Set the i'th led to red
        rgbarray[i] = CHSV(hue++, 255, 255);
        // Show the leds
        FastLED.show();
        // now that we've shown the leds, reset the i'th led to black
        // leds[i] = CRGB::Black;
        fadeall();
        // Wait a little bit before we loop around and do it again
        delay(10);
    }
  }

  void _every10() {
    for (int i = 0; i < LED_HEIGHT; i++) {
      for(int j = 9; j < LED_WIDTH; j+=10) {
          // Set the i'th led to red
          rgbarray[j + i*LED_WIDTH] = CHSV(0 + i * 30, 255, 255);
          FastLED.show();
          delay(100);
      }
    }
  }

  void _solid() {
    static uint8_t hue = 0;

    for (int i = 0; i < LED_HEIGHT; i++) {
      for(int j = 0; j < LED_WIDTH; j++) {
          // Set the i'th led to red
          rgbarray[j + i*LED_WIDTH] = CHSV(hue, 255, 255);

      }
    }
    hue = hue + 3;
    FastLED.show();
    delay(100);
  }

  void _identifier() {
    static uint8_t hue = 0;

    for (int i = 0; i < LED_HEIGHT; i++) {
      for(int j = 0; j < LED_WIDTH; j++) {
        if ((j % (i + 2)) > 0) {
          rgbarray[j + i*LED_WIDTH] = CHSV(hue + i*30, 255, 255);
        } else {
          rgbarray[j + i*LED_WIDTH] = CRGB::Black;
        }
      }
    }
    hue = hue + 3;
    FastLED.show();
    delay(100);
  }

  void loop()
  {
    // good for stress test
    _identifier();
    // good for counting LEDs
    // _every10();
  }
}

void setup()
{
  Serial.begin(115200);
  delay(2000);
  Serial.println("INFO:   Version: 2025.07");
  Serial.printf("INFO:   LED counter: %d pixels, %d LEDs \n", leds.numPixels(), numLeds);
  Serial.println();

  leds.begin();

  Pattern::setup();
}



void loop()
{
  Pattern::loop();
}
