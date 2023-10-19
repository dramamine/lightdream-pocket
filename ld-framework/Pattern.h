#include <Arduino.h>
#include <OctoWS2811.h>

class Pattern {
public:
  Pattern(OctoWS2811 *_leds) : leds(_leds) {}; 

  void setup();
  void loop();

private:
  int ticks;
  OctoWS2811 *leds;
};
