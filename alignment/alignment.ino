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

Copyright (c) 2018-2024 Marten Silbiger
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

// i.e. LEDs per output.
#define LED_WIDTH 600

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 8

#define version "2025.04"

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

// @TODO maybe only enable this for the orange/purple brains?
bool serialVisualizerEnabled = true;

// ~~ end config ~~

// how many universes per strip?
const int universesPerStrip = 3;

const int maxUniverses = LED_HEIGHT * universesPerStrip;

const int numLeds = LED_WIDTH * LED_HEIGHT;
DMAMEM int displayMemory[LED_WIDTH * 6];
int drawingMemory[LED_WIDTH * 6];
const int config = WS2811_RGB | WS2811_800kHz;
OctoWS2811 leds(LED_WIDTH, displayMemory, drawingMemory, config);

// Artnet settings
Artnet artnet;

byte timeOffset = 0;

// used for test pattern, not Artnet.
byte ledsPerLayer[] = {
  72,
  66,
  60,
  54,
  48,
  45,
  39,
  33,
  27,
  21,
  15,
  12,
  6
};

// used for test pattern, not Artnet.
byte blanksPerLayer[] = {
  5,
  7,
  5,
  6,
  6,
  4,
  5,
  6,
  5,
  5,
  6,
  5,
  5
};

uint8_t layers = 13;

// used for data coming from Artnet. Remember that the way the triangle rows are
// arranged within the universes is complex (to reduce the number of universes sent)
//
// [universe, DMX starting channel, leds per layer, blanks per layer, adjustment]
int layerDescription[13][5] = {
  {0, 1, 72, 5, 0},
  {0, 217, 66, 7, 0},
  {1, 513, 60, 7, -2},
  {1, 693, 54, 6, 1},
  {1, 855, 48, 7, -1},
  {2, 1025, 45, 6, 0},
  {2, 1160, 39, 5, -1},
  {2, 1277, 33, 6, 1},
  {2, 1376, 27, 7, -1},
  {2, 1457, 21, 7, 2},
  {0, 415, 15, 7, 2},
  {0, 460, 12, 6, 2},
  {1, 999, 6, 5, 0}
};

int adjustmentLayers[8][13] = {
  {0, 0, -2, 1, -1, 0, -1, 1, -1, 2, 2, 2, 0}, // default
  {0, -2, -2, -2, -1, -2, -1, -1, -1, 0, 0, 0, 0}, // 4/20/2025 leftward slightly off
  {0, 0, -1, 1, 2, 3, 5, 5, 6, 8, 10, 10, 10}, // 4/12/2025 rightward trunk modified
  {0, 0,  0,  3, 5, 7, 9, 12, 14, 16, 20, 21, 23}, // 4/16/2025 rightward started 2 lanes early
  {0, 0, -2, 1, -1, 0, -1, 1, -1, 2, 2, 2, 0}, // default
  {0, 0, -2, 1, -1, 0, -1, 1, -1, 2, 2, 2, 0}, // default
  {0, 0, -2, 1, -1, 0, -1, 1, -1, 2, 2, 2, 0}, // default
  {0, 0, -2, 1, -1, 0, -1, 1, -1, 2, 2, 2, 0}, // default
};

namespace Alignment {
  int adjustmentOptions[] = {0, 0, 0, 0, 0, 0, 0, 0};
  int alignmentSelection = 0;

  int _lookupAdjustment(int layer, int whichTriangle) {
    // which adjustment are we using?
    int adjIdx = adjustmentOptions[whichTriangle];
    // if (whichTriangle == 0) {
    //   Serial.printf("Using adjustment index: %d\n", adjIdx);
    // }

    // look up by layer
    return adjustmentLayers[adjIdx][layer];
  }
}

namespace SerialVisualizerSender {
  int frameIdx = 504;

  void setup()
  {
    Serial1.begin(9600);   // Hardware Serial1 on pins 0 (TX) and 1 (RX)
    Serial.println("Serial visualizer sender ready.");
  }

  void send(uint8_t *frame) {
    int r = frame[frameIdx];
    int g = frame[frameIdx + 1];
    int b = frame[frameIdx + 2];
    // Serial.printf("Sending: %d,%d,%d\n", r, g, b);
    Serial1.printf("%d,%d,%d\n", r, g, b);
  }
}

namespace Pattern {

  const int BRIGHTNESS = 50; // out of 255
  int ticks = 0;

  long getLedColorHSV(byte h, byte s, byte v)
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


  void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
  }

  long _getLayerColor(uint8_t layer) {
    const int highlightedLayer = (ticks / 3) % (layers + 7);
    int distance = abs( (layer+2) - highlightedLayer );
    // Serial.printf("debug layer color (highlighted, layer, distance): %d, %d, %d\n", highlightedLayer, layer, distance);
    if (distance >= 3) {
      return getLedColorHSV(0, 0, 0);
    } else if (distance == 2) {
      return getLedColorHSV(ticks/4 % 256, 255, 25);
    } else if (distance == 1) {
      return getLedColorHSV(ticks/4 % 256, 255, 50);
    } else {
      return getLedColorHSV(ticks/4 % 256, 255, BRIGHTNESS);
    }
  }

  void _rippleLayers() {
    uint16_t i = 0;
    for (int layer=0; layer<layers; layer++) {
      long color = _getLayerColor(layer);
      for (uint8_t j=0; j < ledsPerLayer[layer]; j++) {
        for (int k=0; k<LED_HEIGHT; k++) {
          leds.setPixelColor(i+LED_WIDTH*k, color);
        }
        i++;
      }
      for (uint8_t j=0; j < blanksPerLayer[layer]; j++) {
        // always black, so don't need to set color
        i++;
      }
      // Serial.printf("after layer, i was: %d\n", i);
    }

    leds.show();
  }
  int _countPreviousLeds(int layer) {
    int total = 0;
    for (int i=layer-1; i>= 0; i--) {
      total += ledsPerLayer[i];
      total += blanksPerLayer[i];
    }
    return total;
  }

  void _blankEverything() {
    for (int i=0; i<LED_HEIGHT*LED_WIDTH; i++) {
      leds.setPixelColor(i, getLedColorHSV(0, 0, 0)); // set to black
    }
  }

  void _doAlignmentPattern() {
    for (int t=0; t<LED_HEIGHT; t++) {
      // only show alignment value sometimes
      if (!(Alignment::alignmentSelection == (t+1) || Alignment::alignmentSelection == 9)) {
        continue;
      }
      int ledIdx = 0;
      for (int i=0; i<layers; i++) {

        int ledsPerLayer = layerDescription[i][2];
        int adjustment = Alignment::_lookupAdjustment(i, t);

        ledIdx = t*LED_WIDTH + _countPreviousLeds(i) + adjustment;

        for (int led = 0; led < ledsPerLayer; led++) {
          // see if LED is within 3 pixels of the midpoint of ledsPerLayer
          int midpoint = ledsPerLayer / 2;
          if (abs(led - midpoint) <= 3) {
            leds.setPixelColor(ledIdx, getLedColorHSV(led * 10 % 256, 255, BRIGHTNESS));
            // leds.setPixelColor(ledIdx, getLedColorHSV(0, 255, BRIGHTNESS));
          } else {
            leds.setPixelColor(ledIdx, getLedColorHSV(0, 0, 0)); // set to black
          }
          ledIdx++;
        }
      }
    }


    leds.show();
  }

  void loop()
  {
    _rippleLayers();
    ticks++;
  }

  void intro() {
    _rippleLayers();
  }
}

namespace Networking {
  // Teensy serial to IP address
  const byte pairs = 9;
  int _macToIpPairs[pairs][2] = {
    {0xCB, 31}, // 00-15-B5-CB red i.e. "top"
    {0xDA, 32}, // 00-10-16-DA orange
    {0xFE, 32}, // 00-0C-35-FE silver (general prototyping)
    {0xF4, 32}, // 00-0C-35-FE silver (general prototyping)
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

    for (int i=0; i<pairs; i++) {
      if (_macToIpPairs[i][0] == serial[3]) {
        Serial.println("INFO:   Used serial to figure out which brain I am.");
        _fakemac[5] = _macToIpPairs[i][0];
        _ip[3] = _macToIpPairs[i][1];
      }
    }
  }

  // for a given layer, sum LEDs up to this layer
  int _countPreviousLeds(int layer) {
    int total = 0;
    for (int i=layer-1; i>= 0; i--) {
      total += ledsPerLayer[i];
      total += blanksPerLayer[i];
    }
    return total;
  }

  void _copyFrameToLeds(uint8_t *frame, int len, int ledIdx, int frameIdx) {
    for (int i=0; i<len; i++) {
      leds.setPixel(
        ledIdx+i,
        frame[frameIdx + 3*i],
        frame[frameIdx + 3*i+1],
        frame[frameIdx + 3*i+2]
      );
    }
  }

  void _updateLedRow(uint8_t *frame, int layer, int uni, int dmxPosition, int adjustment) {
    int uniOffset = uni % 3;
    int panelOffset = floor(uni / 3) * LED_WIDTH;
    // Serial.printf("Using offset %d for universe %d\n", panelOffset, uni);
    return _copyFrameToLeds(
      frame,
      layerDescription[layer][2],
      panelOffset + _countPreviousLeds(layer) + adjustment,
      dmxPosition - uniOffset*512 - 1
    );
  }



  int ctr = 0;
  // @TODO consider move this to alignment namespace
  void _considerUpdateMapping(uint8_t *frame, int uni) {
    int uniOffset = uni % 3;
    int whichTriangle = uni / 3;

    if (whichTriangle > 0) {
      return;
    }
    if (uniOffset > 0) {
      return;
    }
    ctr++;
    if (ctr < 201) {
      return;
    }
    ctr = 0;

    // Serial.printf("update mapping running: %d %d \n", uniOffset, whichTriangle);
    Serial.printf("My frame values are now: (%d) %d %d %d %d %d %d %d %d (%d) \n",
      frame[494],
      frame[495],
      frame[496],
      frame[497],
      frame[498],
      frame[499],
      frame[500],
      frame[501],
      frame[502],
      frame[503]
    );

    for (int i=0; i<8; i++) {
      int fdx = 495 + i;
      int fval = round(frame[fdx]/16);
      if (fval < 8) { // max limit for options
        Alignment::adjustmentOptions[i] = fval;
      } else {
        // Serial.printf("Got value outside acceptable range: %d %d", frame[fdx], fval);
      }
    }

    int oldValue = Alignment::alignmentSelection;
    Alignment::alignmentSelection = round(frame[503] / 16);
    if (oldValue != Alignment::alignmentSelection) {
      Pattern::_blankEverything();
    }

    Serial.printf("My adj options are now: %d %d %d %d %d %d %d %d \n",
      Alignment::adjustmentOptions[0],
      Alignment::adjustmentOptions[1],
      Alignment::adjustmentOptions[2],
      Alignment::adjustmentOptions[3],
      Alignment::adjustmentOptions[4],
      Alignment::adjustmentOptions[5],
      Alignment::adjustmentOptions[6],
      Alignment::adjustmentOptions[7]
    );

  }

  void updateLeds(int uni) {
    uint8_t *frame = artnet.getDmxFrame();

    // consider update serial visualizer
    if (uni == 0 && serialVisualizerEnabled) {
      SerialVisualizerSender::send(frame);
    }

    int uniOffset = uni % 3;
    int whichTriangle = uni / 3;

    for (int i=0; i<layers; i++) {
      int uniFromDescription = layerDescription[i][0];
      if (uniFromDescription != uniOffset) {
        // why?? does this happen?
        continue;
      }

      _considerUpdateMapping(frame, uni);

      int dmxPosition = layerDescription[i][1];

      int adjustment = Alignment::_lookupAdjustment(i, whichTriangle);
      // int adjustment = layerDescription[i][4];
      _updateLedRow(frame, i, uni, dmxPosition, adjustment);
    }

    if (Alignment::alignmentSelection > 0) {
      Pattern::_doAlignmentPattern();
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
      // Serial.println("calling leds.show()");
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
          // black out each LED
          for (int i = 0; i < numLeds; i++)
          {
            leds.setPixel(i, 0, 0, 0);
          }
          leds.show();
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
  Serial.printf("INFO:   LED counter: %d pixels, %d LEDs \n", leds.numPixels(), numLeds);
  Serial.println();

  leds.begin();
  Pattern::setup();
  Pattern::intro();

  SerialVisualizerSender::setup();

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


  if (!Networking::hasReceivedArtnetPacket)
  {
    Pattern::loop();
  }
}
