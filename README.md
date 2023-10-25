## Lightdream Pocket

This repo contains various programs I run on a Teensy 4.1 + OctoWS2811 adapter to control thousands of LEDs.

## ld-framework

This code is the basis for the other examples. By default, it expects the Teensy to be connected to a network switch. While receiving Artnet data, it'll display that data. If it's not receiving Artnet data, it will play "demo" code (as written in Pattern.cpp).

The use case here is, you have a more complex setup (with Resolume, etc.) but you want some sort of fallback pattern to play when Artnet is down.

## artnet-to-sd-card

Python code to convert Resolume + Lightjams recordings to SD card format (for the programs below)

## ld-artnet-videosdcard

While receiving Artnet data, display that data. Otherwise, play recorded DMX off of the SD card.

## ld-videosdcard

Play recorded DMX off of the SD card.

## ld-artnet-triangles

Code for running the triangle panels for [Super Lightdream](https://super-lightdream.metalheart.org/)

## ld-artnet-fastled

@TODO

## ld-fastled

@TODO

## ld-super-lightdream

@TODO
