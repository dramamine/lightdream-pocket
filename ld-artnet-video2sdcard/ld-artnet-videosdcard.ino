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

#include <SD.h>
#include <Wire.h>

#define FILENAME     "output-340x4.bin"
#define DELAY_WHEN_RELOADING_FILE  0

// i.e. LEDs per output. 
#define LED_WIDTH 340

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 4

// if true, program expects to be plugged into a network switch. If it's not,
// it will get stuck at `setup()::artnet.begin()`.
// ## Troubleshooting the network
// If you see "Link status (should be 2)" 
bool useNetwork = true;

// how many strips? Octo board supports 8 data channels out
// const byte LED_HEIGHT = 4;        // change for your setup

// Send fps timing to Serial out, should be around 40 fps
bool showFps = false;

// how long is our update look taking to render?
// for reference: runs about 12us for regular, 32-universe code
// LD algorithm Q3-2023 was running 15-17us for 8-universe code
bool showTiming = false;

// ~~ end config ~~

// how many universes per strip?
const int universesPerStrip = ceil(LED_WIDTH / 170.0);

// make sure the config above is correct for your setup. we expect the controlling 
// software  to send (LED_HEIGHT * universesPerStrip) universes to this IP.
const int ledsPerUniverse = 170;


// const int ledsPerStrip = ledsPerUniverse * universesPerStrip;
const int maxUniverses = LED_HEIGHT * universesPerStrip;

const int numLeds = LED_WIDTH * LED_HEIGHT;
DMAMEM int displayMemory[LED_WIDTH * 6];
int drawingMemory[LED_WIDTH * 6];
const int config = WS2811_GRB | WS2811_800kHz;
OctoWS2811 leds(LED_WIDTH, displayMemory, drawingMemory, config);

// Artnet settings
Artnet artnet;

namespace Pattern {
  elapsedMicros elapsedSinceLastFrame = 0;
  bool playing = false;
  File videofile;


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
  }

  void loop()
  {
    unsigned char header[5];

    if (playing) {
      if (sd_card_read(header, 5)) {
        // Serial.printf("my header: %u %u %u %u %u\n", header[0], header[1], header[2], header[3], header[4]);
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
          // unsigned int usec = 10000; // 100 fps
          // unsigned int usec = 4166; // 240 fps
          unsigned int readsize = size;
          // Serial.printf("size and usec: %u %u\n", size, usec);
          if (readsize > sizeof(drawingMemory)) {
            readsize = sizeof(drawingMemory);
          }
          if (sd_card_read(drawingMemory, readsize)) {
            // Serial.printf(", us = %u", (unsigned int)elapsedSinceLastFrame);
            // Serial.println();
            while (elapsedSinceLastFrame < usec) ; // wait
            elapsedSinceLastFrame -= usec;
            leds.show();
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
          return;
        }
      } else {
        error("unable to read 5-byte header");
        return;
      }
    } else {
      delay(DELAY_WHEN_RELOADING_FILE);
      videofile = SD.open(FILENAME, FILE_READ);
      if (videofile) {
        Serial.println("File opened");
        playing = true;
        elapsedSinceLastFrame = 0;
      }
    }
  }
}

namespace Networking {
  // CHANGE FOR YOUR SETUP most software this is 1, some software send out artnet first universe as 0.
  const int startUniverse = 0; 

  // have we received data for each universe?
  bool universesReceived[maxUniverses];

  // for calculating data received rates
  int universesReceivedTotal[maxUniverses];

  // true once we have received an Artnet packet
  bool hasReceivedArtnetPacket = false;

  bool sendFrame = 1;

  // Change ip for your setup, last octet is changed in updateIp()
  byte _ip[] = {169, 254, 18, 0};
  byte _fakemac[] = {0x04, 0xE9, 0xE5, 0x00, 0x69, 0xEC};


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

    byte hardcoded_addresses[5] = {32, 33, 34, 35, 36};
    uint8_t serials[5] = {
        //0xDA, // 00-10-16-DA orange
        0x9D, // LED Door 
        // 0xFE, // replacing this for prototyping
        0x5E, // 00-0C-46-5E yellow
        0x5D, // 00-0C-46-5D green - motherbrain
        0x92, // 00-0C-46-92 blue
        0x70, // 00-0C-46-70 purple
    };
    
    for (int i = 0; i < 5; i++)
    {
      if (serials[i] == serial[3])
      {
        Serial.println("INFO:   Used serial to figure out which brain I am.");
        _ip[3] = hardcoded_addresses[i];
        _fakemac[5] = hardcoded_addresses[i];
      }
    }
  }

  // call setPixel using frame data.
  void updateLeds(int uni)
  {
    if (universesReceived[uni] > 0)
      return;
    universesReceived[uni] = 1;

    if (uni >= maxUniverses) {
      Serial.printf("WARN:   Got a universe of data that we weren't expecting. %d\n", uni);
      return;
    }

    int length = artnet.getLength();
    uint8_t *frame = artnet.getDmxFrame();
    universesReceivedTotal[uni] = universesReceivedTotal[uni] + 1;

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

    Serial.println("STATUS: Connected to network switch & listening for Artnet data.");


    artnet.begin(_fakemac, _ip);

    Serial.print("INFO:   Local ip: ");
    Serial.println(Ethernet.localIP());

  }


  // print fps and how many frames we've received from each universe. this
  // prints incrementally (every 100 frames, when universe 0 is received)
  void printFps() {
    int uni = artnet.getUniverse();
    for (int i=0; i<10; i++) {
      artnet.getUniverse();
    }
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
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("INFO:   Version: 2023.10");
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
