#include <Arduino.h>
#include <OctoWS2811.h>
#include "Pattern.h"

const int fps = 11;
const int BRIGHTNESS = 50;

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

void Pattern::setup() {
  ticks = 0;
  rainbowSetup();
}

void Pattern::loop()
{
  
  leds->setPixel(1,hues[((140+ticks) % 255)]);
  leds->setPixel(2,hues[((170+ticks) % 255)]);
  leds->setPixel(3,hues[((200+ticks) % 255)]);
  leds->setPixel(4,hues[((230+ticks) % 255)]);
  leds->show();
  ticks++;
  delay((int)1000/fps);
}
