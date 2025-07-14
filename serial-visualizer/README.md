## Serial Visualizer

### Hardware Setup

The serial visualizer runs on a Teensy 4.x with the Octo adapter.

It's hooked up to another Teensy 4.1 running the Bermuda brain code (>= 2025.05) - introduced with the "alignment" sketch. There's a variable in that code "serialVisualizeEnabled". If so, that code sends color data out on pin 1 (TX).

The serial visualizer receives data on pin 0 (RX).

The Teensys are also connected via the +5V pin and GND pin for power.