## Converting from Lightjams to SD Card Format

@TODO command line not working yet, just edit the constants in the files included

### Basic Example: 170 x 8 LEDs or less

#### From Resolume:
![](https://i.imgur.com/jnTsAPk.png)

Set up your advanced output to send to up to 8 universes (0-7) with up to 170 LEDs per universe.
Reduce delay to 0ms.
Set IP to "Broadcast".
Set framerate on all your outputs to a value such as 40 fps.

#### From Lightjams:

https://www.lightjams.com/recorder.html

![](https://i.imgur.com/osEPK6G.png)

Set your adapter to match Resolume's output settings (under Preferences > DMX > Network Adapter)
Set your framerate to match your Advanced Output framerate (ex. 40 fps)
Set "count" to 8 universes.
Confirm that you see the correct FPS and you see some data output in the top of the window.

At this point, you can press Record and Stop to capture your data. Any compression level is fine, but I prefer "ultrafast" for largest file size / less CPU power to play back.


#### From this repo:

```bash
pip install -r requirements.txt

# default
python video2sdcard.py lightjams.mp4

# specify width, height, output file
python video2sdcard.py lightjams.mp4 --width=170 --height==8 --fps=30.0 --output=output.bin
```

#### Teensy code:

First, copy `output.bin` onto an SD card. Place this card in your Teensy 4.1.

Edit `videosdcard.ino` and update the following variables to match your output settings:

```c++
#define LED_WIDTH    170   // number of LEDs horizontally
#define LED_HEIGHT   8   // number of LEDs vertically (must be multiple of 8)

#define FILENAME     "output.bin"
```

Upload this source file to your Teensy. You can do this using [Teensyduino](https://www.pjrc.com/teensy/teensyduino.html).
You can check the Serial monitor once your script is running to confirm you don't see any error messages. A common error you might see is "unable to read header"; this can happen if there's a mismatch between your stated data size (LED_WIDTH * LED_HEIGHT) and the data size actually present in `output.bin`.


### More data: sending more than 170 LEDs per strand (up to 8 strands)

Say you've got fixtures with more than 170 pixels that you want to treat as one strand on your Teensy:
https://i.imgur.com/OMnzS34.png

This requires 2 universes worth of data to send.
https://i.imgur.com/BjXOalP.png

Make sure each strand starts with the correct universe. Here I've got Lumiverse 1 sending to universes 0-1, and Lumiverse 2 sending to universes 2-3, etc. up to Lumiverse 8 sending to universes 14-15.
https://i.imgur.com/HFRH2az.png

In Lightjams, we'd set our number of universes correctly to match the total outputted data.

In our script we'd run:

```bash
python video2sdcard.py lightjams.mp4 --width=200 --height==8 --fps=30.0 --output=output.bin
```

Where 2 matches the number of strands to output (not the total # of universes) - the script will validate the # of universes per strand based on the given width and the video frames provided.

This works best when sending to 8 strands (you can send blank data to unused strands)