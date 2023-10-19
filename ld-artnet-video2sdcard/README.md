## ld-framework

In the main sketch, set a couple variables for your setup:

```cpp
#define FILENAME     "output-340x4.bin"

// if true, program expects to be plugged into a network switch. If it's not,
// it will get stuck at `setup()::artnet.begin()`.
// ## Troubleshooting the network
// If you see "Link status (should be 2)" 
bool useNetwork = true;

// i.e. LEDs per output. 
#define LED_WIDTH 340

// i.e. how many strips; Octo board supports 8 channels out
#define LED_HEIGHT 4
```

For these variables, say you are sending the maximum amount of data to this controller. In that case you would set LED_HEIGHT to 8 (which uses all 8 of the Octo adapter's outputs), and you'd set LED_WIDTH to 170*4 (which means Resolume is outputting 4 universes for each strip, or 32 universes total).
- If you don't send enough universes, this controller will wait forever for the missing universes and will appear to not work
- If you configure this wrong, your mapping will be off
- "LEDs per universe" is probably 170 forever since the DMX protocol has 512 bytes per universe. Resolume just discards the last 2 bytes which is what we do here as well.

If you set `useNetwork` to false, you'll read the file on the SD card without requiring any network setup.

IP address code here is specific to Lightdream, specifically for assigning an IP address to each microcontroller based on its serial number.
