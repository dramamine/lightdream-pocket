"""
Simplified OPC Serial Sender for TouchDesigner
Place this code in a Text DAT with Language set to Python

Usage:
1. Initialize once: opc_init('COM35')
2. Send frames: opc_send_top('constant1') or opc_send_test()
"""

# Matrix config - must match smartmatrix-serial.ino
MATRIX_WIDTH = 64
MATRIX_HEIGHT = 192
NUM_PIXELS = MATRIX_WIDTH * MATRIX_HEIGHT

# Global variables
opc_serial = None
opc_connected = False

def opc_init(port='COM35', baudrate=115200):
    """Initialize OPC serial connection"""
    global opc_serial, opc_connected

    try:
        import serial
        if opc_serial and opc_serial.is_open:
            opc_serial.close()

        opc_serial = serial.Serial(port, baudrate, timeout=1.0)
        opc_connected = True
        print(f"OPC connected to {port}")
        return True
    except Exception as e:
        print(f"OPC connection failed: {e}")
        opc_connected = False
        return False

def opc_send_raw(pixel_data):
    """Send raw pixel data as OPC frame"""
    global opc_serial, opc_connected

    if not opc_connected:
        return False

    try:
        import struct

        # Ensure we have the right amount of data
        if len(pixel_data) < NUM_PIXELS * 3:
            # Pad with black pixels
            pixel_data.extend([0] * (NUM_PIXELS * 3 - len(pixel_data)))
        elif len(pixel_data) > NUM_PIXELS * 3:
            # Truncate
            pixel_data = pixel_data[:NUM_PIXELS * 3]

        # Create OPC header: [channel][command][length_hi][length_lo]
        data_length = len(pixel_data)
        header = struct.pack('>BBH', 0, 0, data_length)

        # Send frame
        frame = header + bytes([int(x) for x in pixel_data])
        opc_serial.write(frame)
        opc_serial.flush()
        return True

    except Exception as e:
        print(f"OPC send failed: {e}")
        return False

def opc_send_top(top_path='mapped_video_top1'):
    """Send frame from a TOP"""
    if not opc_connected:
        opc_init()

    try:
        top = op(top_path)
        pixel_data = []

        # Sample the texture
        for y in range(MATRIX_HEIGHT):
            for x in range(MATRIX_WIDTH):
                u = x / MATRIX_WIDTH
                v = y / MATRIX_HEIGHT
                r, g, b, a = top.sample(u, v)
                pixel_data.extend([r * 255, g * 255, b * 255])

        return opc_send_raw(pixel_data)

    except Exception as e:
        print(f"TOP sampling failed: {e}")
        return False

def opc_send_test():
    """Send rainbow test pattern"""
    if not opc_connected:
        opc_init()

    pixel_data = []
    for y in range(MATRIX_HEIGHT):
        for x in range(MATRIX_WIDTH):
            # Rainbow based on position
            hue = ((x + y) % 64) / 64.0

            # Simple HSV to RGB
            import colorsys
            r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 0.8)
            pixel_data.extend([r * 255, g * 255, b * 255])

    return opc_send_raw(pixel_data)

def opc_close():
    """Close serial connection"""
    global opc_serial, opc_connected
    if opc_serial and opc_serial.is_open:
        opc_serial.close()
    opc_connected = False