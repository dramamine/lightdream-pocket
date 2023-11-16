## Lightdream Pocket

This repo contains various programs I run on a Teensy 4.1 + OctoWS2811 adapter to control thousands of LEDs.

This code is mostly rearrangements of code & ideas from [PaulStoffregen](https://github.com/PaulStoffregen/OctoWS2811) to make them easier for me to do my specific tasks:
- handle Artnet data via network
- play back video content
- play back patterns written in code
- LED mapping for my custom fixtures

Specifically, each sketch contains two namespaces:

- Networking: code related to networking
  - assigning IP addresses
  - tracking performance

- Pattern: decide what to show when networking disabled or no data being sent
  - setup() called with the main setup() call
  - loop() called with the main loop() call
  - keep track of `ticks` to know how many frames have been sent so far


---

## ld-framework

This code is the basis for the other examples. By default, it checks the variable `useNetwork` and if true, the Teensy must be connected to a network switch. While receiving Artnet data, it'll display that data. If it's not receiving Artnet data, it will play fallback "demo" code (as written in the Pattern namespace).

The use case here is, you have a more complex setup (with Resolume, etc.) but you want some sort of fallback pattern to play when Artnet is down.

This outputs rainbow patterns 800x8 which is helpful for testing your hardware setup.

---

## artnet-to-sd-card

Python code to convert Resolume + Lightjams recordings to SD card format (for the programs below)

## ld-artnet-fastled

While receiving Artnet data, display that data. Otherwise, use the FastLED library to implement code-based patterns. For example, basically [all of these FastLED example sketches](https://github.com/FastLED/FastLED/tree/master/examples) are quick to add here.


## ld-artnet-triangles

Code for running the triangle panels for [Super Lightdream](https://super-lightdream.metalheart.org/)


## ld-artnet-videosdcard

While receiving Artnet data, display that data. Otherwise, play recorded DMX off of the SD card.

