# TouchDesigner to SmartMatrix Serial Setup Guide

This guide shows how to send LED data directly from TouchDesigner to `smartmatrix-serial.ino` via OPC (Open Pixel Control) over serial.

## Hardware Setup
- Teensy 4.1 with SmartMatrix Shield V5
- HUB75 LED matrix panels: 64x192 pixels (3 panels of 64x64)
- Serial connection via USB

## TouchDesigner Project Setup

### 1. Import the OPC Serial Script

1. Create a **Text DAT**
2. Set the **Language** parameter to **Python**
3. Copy the contents of `touchdesigner-opc-sender.py` into the Text DAT
4. Name it something like `opc_serial`

### 2. Create Your LED Content

#### Option A: Using a TOP (Texture)
1. Create your visual content using any TOP operators (Constant, Noise, Movie File In, etc.)
2. Make sure the resolution matches your matrix: **64x192 pixels**
3. Connect your content to a **Fit TOP** to ensure correct dimensions

#### Option B: Using a CHOP (Channel Data)
1. Create channel data with RGB values (0.0 to 1.0 range)
2. Each channel should have 12,288 samples (64×192 pixels)
3. Name the channels 'r', 'g', 'b'

### 3. Set Up Triggering

#### Method 1: Timer-based sending (recommended)
1. Create a **Timer CHOP**
2. Set **Rate** to your desired frame rate (e.g., 30 FPS)
3. Create an **Execute DAT**
4. Reference the timer in the Execute DAT's **CHOP** parameter
5. In the Execute DAT, add this code:
```python
# Initialize - run once
op('opc_serial').module.initialize('COM35')  # Change COM port as needed

def onOffToOn(channel, sampleIndex, val, prev):
    # Called when timer pulses
    # Send frame from a TOP
    op('opc_serial').module.send_frame_from_top('fit1')
    
    # OR send frame from a CHOP
    # op('opc_serial').module.send_frame_from_chop('chop1')
```

#### Method 2: Manual triggering
1. Create a **Button COMP**
2. Create an **Execute DAT**
3. Set the Execute DAT to watch the button
4. Add this code:
```python
def onOffToOn(channel, sampleIndex, val, prev):
    op('opc_serial').module.send_test_pattern()
```

### 4. LED Mapping Considerations

The `smartmatrix-serial.ino` expects pixels in this order:
- **Linear mapping**: `pixel_index = y * 64 + x`
- **Origin**: Top-left (0,0)
- **Direction**: Left-to-right, top-to-bottom

If your panels are arranged differently, you'll need to add mapping logic in TouchDesigner:

```python
def remap_pixels(width, height, pixel_data):
    """Example: flip Y-axis if panels are mounted upside down"""
    remapped = []
    for y in range(height):
        for x in range(width):
            # Flip Y coordinate
            flipped_y = height - 1 - y
            src_idx = (flipped_y * width + x) * 3
            remapped.extend(pixel_data[src_idx:src_idx+3])
    return remapped
```

## Project Structure Example

```
TouchDesigner Project
├── Video/Content Sources
│   ├── moviefilein1 (your content)
│   ├── noise1 (procedural patterns)
│   └── constant1 (solid colors)
│
├── Processing
│   ├── fit1 (resize to 64x192)
│   ├── level1 (brightness/contrast)
│   └── feedback1 (effects)
│
├── Timing
│   ├── timer1 (30 FPS trigger)
│   └── execute1 (send frames)
│
└── Serial Communication
    ├── opc_serial (Text DAT with Python script)
    └── execute2 (initialization and cleanup)
```

## Testing

1. **Test Connection**:
   ```python
   op('opc_serial').module.initialize('COM35')
   ```

2. **Test Pattern**:
   ```python
   op('opc_serial').module.send_test_pattern()
   ```

3. **Monitor Serial** (optional):
   - Open Arduino IDE Serial Monitor
   - Set baud rate to 115200
   - Look for frame count and FPS messages

## Troubleshooting

### Serial Connection Issues
- Check COM port number in Device Manager
- Ensure Teensy is running `smartmatrix-serial.ino`
- Verify baud rate is 115200

### No Visual Output
- Check matrix power supply
- Verify HUB75 cable connections
- Ensure TouchDesigner content resolution is 64x192

### Performance Issues
- Reduce frame rate in Timer CHOP
- Check TouchDesigner performance monitor
- Consider using lower resolution for complex content

### Pixel Mapping Issues
- Verify panel arrangement matches expected layout
- Test with simple patterns (solid colors, gradients)
- Add debug prints to see pixel data values

## Advanced Features

### Custom Pixel Mapping
Create a **Table DAT** with pixel mapping coordinates and reference it in your sending script.

### Multiple Matrix Support
Modify the script to support multiple serial ports for larger installations.

### Real-time Parameter Control
Use TouchDesigner's parameter system to control brightness, effects, and patterns in real-time.