"""
TouchDesigner OPC Serial Sender
Python script to be used as a Text DAT in TouchDesigner
Sends OPC (Open Pixel Control) data over serial to smartmatrix-serial.ino

Usage in TouchDesigner:
1. Create a Text DAT
2. Set language to Python
3. Paste this code
4. Connect your LED pixel data to the script
5. Call sendFrame(pixelData) from a Timer CHOP or Execute DAT

Matrix Configuration (must match Teensy):
- Width: 64 pixels
- Height: 192 pixels (64*3 panels)
- Total: 12,288 pixels
- Serial: 115200 baud
"""

import serial
import struct
import time
import numpy as np

# Matrix configuration (must match smartmatrix-serial.ino)
MATRIX_WIDTH = 64
MATRIX_HEIGHT = 192  # 64*3
NUM_PIXELS = MATRIX_WIDTH * MATRIX_HEIGHT

# OPC Protocol constants
OPC_CHANNEL = 0
OPC_COMMAND_SET_PIXELS = 0

class OPCSerial:
    def __init__(self, port='COM35', baudrate=115200):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.connected = False

    def connect(self):
        """Connect to serial port"""
        try:
            if self.serial and self.serial.is_open:
                self.serial.close()

            self.serial = serial.Serial(self.port, self.baudrate, timeout=1.0)
            self.connected = True
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"Failed to connect to {self.port}: {e}")
            self.connected = False
            return False

    def disconnect(self):
        """Disconnect from serial port"""
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.connected = False

    def send_frame(self, pixel_data):
        """
        Send OPC frame over serial
        pixel_data should be a list/array of (r,g,b) tuples or flat RGB array
        """
        if not self.connected:
            return False

        try:
            # Convert pixel data to bytes
            if isinstance(pixel_data[0], (list, tuple)):
                # Format: [(r,g,b), (r,g,b), ...]
                rgb_bytes = []
                for pixel in pixel_data[:NUM_PIXELS]:
                    rgb_bytes.extend([int(pixel[0]), int(pixel[1]), int(pixel[2])])
            else:
                # Format: [r,g,b,r,g,b,r,g,b,...]
                rgb_bytes = [int(x) for x in pixel_data[:NUM_PIXELS*3]]

            # Pad if needed
            while len(rgb_bytes) < NUM_PIXELS * 3:
                rgb_bytes.extend([0, 0, 0])

            # Create OPC header
            data_length = len(rgb_bytes)
            header = struct.pack('>BBH', OPC_CHANNEL, OPC_COMMAND_SET_PIXELS, data_length)

            # Send header + pixel data
            frame = header + bytes(rgb_bytes)
            self.serial.write(frame)
            self.serial.flush()

            return True

        except Exception as e:
            print(f"Failed to send frame: {e}")
            return False

# Global OPC sender instance
opc_sender = None

def initialize(port='COM35'):
    """Initialize OPC serial connection"""
    global opc_sender
    opc_sender = OPCSerial(port)
    return opc_sender.connect()

def send_frame_from_top(top_path):
    """
    Send frame from a TOP (texture operator)
    top_path: path to a TOP operator (like 'constant1' or '/project1/constant1')
    """
    global opc_sender

    if not opc_sender or not opc_sender.connected:
        if not initialize():
            print("Not initialized")
            return False

    try:
        # Get the TOP
        top = op(top_path)
        if not top:
            print(f"TOP not found: {top_path}")
            return False

        # Sample the texture to get pixel data
        # TouchDesigner uses 0-1 float values, we need 0-255 integers
        pixel_data = []

        for y in range(MATRIX_HEIGHT):
            for x in range(MATRIX_WIDTH):
                # Sample pixel at normalized coordinates
                u = x / MATRIX_WIDTH
                v = y / MATRIX_HEIGHT

                # Get RGBA values (0.0 to 1.0) - TouchDesigner sample method
                sample_result = top.sample(u, v, 0)  # Add z coordinate (0 for 2D)
                if len(sample_result) >= 3:
                    r, g, b = sample_result[0], sample_result[1], sample_result[2]
                else:
                    r = g = b = sample_result[0] if len(sample_result) > 0 else 0

                # Convert to 0-255 range
                pixel_data.extend([
                    int(r * 255),
                    int(g * 255),
                    int(b * 255)
                ])
        print("sending frame", pixel_data[:12], "...")
        return opc_sender.send_frame(pixel_data)

    except Exception as e:
        print(f"Error sampling TOP: {e}")
        return False

def send_frame_from_top_ultra_fast(top_path):
    """
    Ultra-fast version - assumes TOP is already exactly 64x192 RGB
    No resizing, minimal processing
    """
    global opc_sender

    if not opc_sender or not opc_sender.connected:
        if not initialize():
            return False

    try:
        # Get the TOP
        top = op(top_path)
        if not top:
            return False

        # Get numpy array - assume it's already the right size
        img_array = top.numpyArray()

        # Fast path: assume image is exactly 192x64x3 (height x width x RGB)
        if img_array.shape == (MATRIX_HEIGHT, MATRIX_WIDTH, 3):
            # Perfect - just convert to uint8 and flatten
            if img_array.dtype != np.uint8:
                if img_array.max() <= 1.0:
                    pixel_data = (img_array * 255).astype(np.uint8).flatten().tolist()
                else:
                    pixel_data = img_array.astype(np.uint8).flatten().tolist()
            else:
                pixel_data = img_array.flatten().tolist()

            return opc_sender.send_frame(pixel_data)

        # If not perfect match, fall back to the resizing version
        else:
            return send_frame_from_top_fast(top_path)

    except Exception as e:
        print(f"Ultra-fast method failed: {e}")
        return send_frame_from_top_fast(top_path)

def send_frame_from_top_fast(top_path):
    """
    High-performance version using vectorized numpy operations
    """
    global opc_sender

    if not opc_sender or not opc_sender.connected:
        if not initialize():
            return False

    try:
        # Get the TOP
        top = op(top_path)
        if not top:
            print(f"TOP not found: {top_path}")
            return False

        # Get numpy array from TOP
        img_array = top.numpyArray()

        # Handle different image formats efficiently
        if len(img_array.shape) == 3:
            height, width, channels = img_array.shape

            # If image is already the right size, just reshape
            if height == MATRIX_HEIGHT and width == MATRIX_WIDTH:
                if channels >= 3:
                    # Perfect match - just extract RGB and reshape
                    rgb_data = img_array[:, :, :3]
                else:
                    # Grayscale - convert to RGB
                    rgb_data = np.stack([img_array[:, :, 0]] * 3, axis=2)
            else:
                # Need to resize - use numpy for efficiency
                import cv2
                # Resize image to exact matrix dimensions
                resized = cv2.resize(img_array, (MATRIX_WIDTH, MATRIX_HEIGHT))
                if len(resized.shape) == 2:
                    # Grayscale after resize
                    rgb_data = np.stack([resized] * 3, axis=2)
                else:
                    rgb_data = resized[:, :, :3]
        else:
            # 2D grayscale image
            height, width = img_array.shape
            if height == MATRIX_HEIGHT and width == MATRIX_WIDTH:
                rgb_data = np.stack([img_array] * 3, axis=2)
            else:
                import cv2
                resized = cv2.resize(img_array, (MATRIX_WIDTH, MATRIX_HEIGHT))
                rgb_data = np.stack([resized] * 3, axis=2)

        # Convert to 0-255 range if needed (vectorized)
        if rgb_data.dtype == np.float32 or rgb_data.dtype == np.float64:
            if rgb_data.max() <= 1.0:
                rgb_data = (rgb_data * 255).astype(np.uint8)
            else:
                rgb_data = rgb_data.astype(np.uint8)

        # Flatten to 1D pixel array (very fast)
        pixel_data = rgb_data.flatten().tolist()

        return opc_sender.send_frame(pixel_data)

    except ImportError:
        print("OpenCV not available, falling back to slower method")
        return send_frame_from_top_alt(top_path)
    except Exception as e:
        print(f"Fast method failed: {e}, falling back to slower method")
        return send_frame_from_top_alt(top_path)

def send_test_pattern():
    """Send a test pattern - rainbow gradient"""
    global opc_sender

    if not opc_sender or not opc_sender.connected:
        if not initialize():
            return False

    pixel_data = []
    for y in range(MATRIX_HEIGHT):
        for x in range(MATRIX_WIDTH):
            # Create rainbow pattern
            hue = (x + y) / (MATRIX_WIDTH + MATRIX_HEIGHT)

            # Simple HSV to RGB conversion
            import colorsys
            r, g, b = colorsys.hsv_to_rgb(hue, 1.0, 0.5)

            pixel_data.extend([
                int(r * 255),
                int(g * 255),
                int(b * 255)
            ])

    return opc_sender.send_frame(pixel_data)

def send_blackout():
    """Send all black pixels - turns off the entire matrix"""
    global opc_sender

    if not opc_sender or not opc_sender.connected:
        if not initialize():
            return False

    # Create all black pixels
    pixel_data = [0, 0, 0] * NUM_PIXELS

    return opc_sender.send_frame(pixel_data)

def cleanup():
    global opc_sender
    if opc_sender:
        opc_sender.disconnect()
