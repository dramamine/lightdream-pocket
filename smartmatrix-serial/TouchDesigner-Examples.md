# TouchDesigner Component Setup Examples

## Execute DAT - Initialization (run once on startup)

```python
# This goes in an Execute DAT with "Panel Open" trigger
def onPanelOpen(panelValue):
    # Initialize OPC serial connection
    op('opc_serial').module.opc_init('COM35')
    print("OPC Serial initialized")
```

## Execute DAT - Frame Sending (triggered by timer)

```python
# This goes in an Execute DAT connected to a Timer CHOP
def onOffToOn(channel, sampleIndex, val, prev):
    # Send current frame from your content TOP
    success = op('opc_serial').module.opc_send_top('out1')
    if not success:
        print("Failed to send frame")

def onOnToOff(channel, sampleIndex, val, prev):
    # Optional: do something when timer goes off
    pass
```

## Button Execute DAT - Test Pattern

```python
# This goes in an Execute DAT connected to a Button COMP
def onOffToOn(channel, sampleIndex, val, prev):
    # Send test pattern when button is pressed
    op('opc_serial').module.opc_send_test()
    print("Test pattern sent")
```

## Parameter Execute DAT - Dynamic Control

```python
# This goes in an Execute DAT watching parameter changes
def onValueChange(channel, sampleIndex, val, prev):
    # Send frame when any monitored parameter changes
    if channel.name == 'brightness':
        # Brightness changed, update display
        op('opc_serial').module.opc_send_top('level1')
    elif channel.name == 'pattern':
        # Pattern selection changed
        if val == 0:  # Test pattern
            op('opc_serial').module.opc_send_test()
        else:  # Content
            op('opc_serial').module.opc_send_top('switch1')
```

## Advanced: Custom Pixel Mapping Execute DAT

```python
# Custom mapping function for different panel arrangements
def remap_for_my_setup(pixel_data):
    """
    Example: If your 3 panels are arranged horizontally instead of vertically
    Original: 64x192 (3 panels stacked vertically)
    Your setup: 192x64 (3 panels side by side)
    """
    remapped = [0] * len(pixel_data)

    for panel in range(3):
        for y in range(64):
            for x in range(64):
                # Source position (horizontal layout)
                src_x = panel * 64 + x
                src_y = y
                src_idx = (src_y * 192 + src_x) * 3

                # Destination position (vertical layout expected by Teensy)
                dst_x = x
                dst_y = panel * 64 + y
                dst_idx = (dst_y * 64 + dst_x) * 3

                # Copy RGB
                remapped[dst_idx:dst_idx+3] = pixel_data[src_idx:src_idx+3]

    return remapped

def onOffToOn(channel, sampleIndex, val, prev):
    # Get pixel data from TOP
    top = op('render1')
    pixel_data = []

    for y in range(64):  # Your actual height
        for x in range(192):  # Your actual width
            u = x / 192
            v = y / 64
            r, g, b, a = top.sample(u, v)
            pixel_data.extend([r * 255, g * 255, b * 255])

    # Remap for Teensy's expected layout
    remapped_data = remap_for_my_setup(pixel_data)

    # Send remapped data
    op('opc_serial').module.opc_send_raw(remapped_data)
```

## Network Setup - Replacing Artnet Bridge

If you want to keep some Artnet compatibility while using TouchDesigner's serial output:

### Option 1: TouchDesigner receives Artnet, outputs Serial
1. Use **Art-Net In DAT** to receive Artnet data
2. Convert Artnet universe data to pixel array
3. Send via serial OPC

### Option 2: TouchDesigner as Artnet source
1. Create your content in TouchDesigner
2. Use **Art-Net Out DAT** to send to your existing Artnet infrastructure
3. Keep `artnet-to-serial-sender.py` for compatibility with other tools

## Performance Tips

### Optimize Frame Rate
```python
# In your timer Execute DAT - skip frames if serial is busy
frame_counter = 0

def onOffToOn(channel, sampleIndex, val, prev):
    global frame_counter
    frame_counter += 1

    # Only send every 2nd frame (30fps -> 15fps)
    if frame_counter % 2 == 0:
        op('opc_serial').module.opc_send_top('out1')
```

### Batch Multiple Sends
```python
# Send to multiple devices/ports
def send_to_all_matrices():
    content = op('out1')

    # Send to first matrix
    op('opc_serial1').module.opc_send_top('out1')

    # Send to second matrix (different port)
    op('opc_serial2').module.opc_send_top('out1')
```

## Cleanup on Exit

```python
# In an Execute DAT with "Panel Close" trigger
def onPanelClose(panelValue):
    # Clean up serial connections
    op('opc_serial').module.opc_close()
    print("OPC Serial closed")
```