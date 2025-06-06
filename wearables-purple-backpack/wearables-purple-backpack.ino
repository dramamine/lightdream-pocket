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
// for the artnet library to load the right ethernet stuff.
// due to edits I made, it now loads NativeEthernet and NativeEthernetUDP
#include "Artnet.h"
#include <SPI.h>
#include <OctoWS2811.h>
#include "TeensyID.h"
#include <FastLED.h>

// i.e. LEDs per output.
#define LED_WIDTH 500

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 4

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
  const int BRIGHTNESS = 50; // out of 255

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

  void loop()
  {
    // good for stress test
    _solid();
    // good for counting LEDs
    // _every10();
  }
}

namespace Networking {
  // Teensy serial to IP address
  int _macToIpPairs[6][2] = {
    {0xFE, 31}, // 00-10-16-DA orange
    {0x9D, 32}, // LED door
    {0x5E, 33}, // 00-0C-46-5E yellow
    {0x5D, 34}, // 00-0C-46-5D green - motherbrain
    {0x92, 35}, // 00-0C-46-92 blue
    {0x70, 36}, // 00-0C-46-70 purple
  };

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

  // In this fn, we use teensySN() to generate a unique "serial number" for this
  // microcontroller. we use that with the `serials` chart to determine which IP
  // to use when joining the network.
  // update the `serials` array when you have new hardware.
  void updateIp()
  {
    // serial number of this Teensy
    uint8_t serial[4];
    teensySN(serial);
    Serial.printf("INFO:   Serial number: %02X-%02X-%02X-%02X \n", serial[0], serial[1], serial[2], serial[3]);

    for (int i=0; i<6; i++) {
      if (_macToIpPairs[i][0] == serial[3]) {
        Serial.println("INFO:   Used serial to figure out which brain I am.");
        _fakemac[5] = _macToIpPairs[i][0];
        _ip[3] = _macToIpPairs[i][1];
      }
    }
  }

  // call setPixel using frame data.
  void updateLeds(int uni)
  {
    if (uni >= maxUniverses) {
      Serial.printf("WARN:   Got a universe of data that we weren't expecting. %d\n", uni);
      return;
    }

    int length = artnet.getLength();
    uint8_t *frame = artnet.getDmxFrame();


    // copy data from Artnet frame to LED buffer
    for (int i = 0; i < length / 3; i++)
    {

      int led = i + uni * ledsPerUniverse;

      if (led < numLeds)
      {
        leds.setPixel(led, frame[i * 3], frame[i * 3 + 1], frame[i * 3 + 2]);
      }
    }
  }

  // https://www.arduino.cc/reference/en/libraries/ethernet/
  void setup()
  {
    Networking::updateIp();

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

    if (uni >= maxUniverses) {
      return;
    }

    // tracking
    universesReceived[uni] = 1;
    universesReceivedTotal[uni] = universesReceivedTotal[uni] + 1;

    if (showFps) {
      Networking::printFps();
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
        sendFrame = 0;
        break;
      }
    }

    if (sendFrame)
    {
      leds.show();
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
  Serial.println("INFO:   Version: 2023.11");
  Serial.printf("INFO:   LED counter: %d pixels, %d LEDs \n", leds.numPixels(), numLeds);
  Serial.println();

  leds.begin();

  // @TODO see if we can handle failure in a cleaner way here
  if (useNetwork) {
    Networking::setup();
  }

  Pattern::setup();
}



void loop()
{
  if (useNetwork) {
    Networking::loop();
  }


  if (!Networking::hasReceivedArtnetPacket)
  {
    Pattern::loop();
  }
}
