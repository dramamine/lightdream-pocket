#!/usr/bin/env python3
"""
OPC (Open Pixel Control) Serial Sender
Sends demo patterns to Teensy running smartmatrix-serial.ino

Usage:
    python artnet-to-serial-sender.py [COM_PORT] [PATTERN]

Examples:
    python artnet-to-serial-sender.py COM3 rainbow
    python artnet-to-serial-sender.py /dev/ttyACM0 solid
"""

import serial
import serial.tools.list_ports
import time
import math
import sys
import struct
from typing import Tuple, List

# Matrix configuration (must match Teensy code)
MATRIX_WIDTH = 64
MATRIX_HEIGHT = 64*3
NUM_PIXELS = MATRIX_WIDTH * MATRIX_HEIGHT

# OPC Protocol constants
OPC_CHANNEL = 0
OPC_COMMAND_SET_PIXELS = 0
OPC_HEADER_SIZE = 4

def find_teensy_ports():
    """Find all available serial ports and identify likely Teensy ports"""
    ports = serial.tools.list_ports.comports()

    print("Available serial ports:")
    teensy_ports = []

    for port in ports:
        port_info = f"  {port.device}"
        if port.description:
            port_info += f" - {port.description}"
        if port.manufacturer:
            port_info += f" ({port.manufacturer})"

        print(port_info)

        # Look for Teensy-specific identifiers
        if port.manufacturer and "teensy" in port.manufacturer.lower():
            teensy_ports.append(port.device)
        elif port.description and ("teensy" in port.description.lower() or
                                 "usb serial" in port.description.lower()):
            teensy_ports.append(port.device)
        elif port.vid == 0x16C0 and port.pid == 0x0483:  # Teensy VID/PID
            teensy_ports.append(port.device)

    if teensy_ports:
        print(f"\nLikely Teensy ports: {teensy_ports}")
        return teensy_ports[0]  # Return first likely Teensy port
    elif ports:
        print(f"\nNo Teensy detected, but found {len(ports)} port(s)")
        return ports[0].device  # Return first available port
    else:
        print("\nNo serial ports found!")
        return None

def test_port_connection(port: str, baudrate: int = 115200):
    """Test if a port can be opened successfully"""
    try:
        with serial.Serial(port, baudrate, timeout=1) as ser:
            print(f"✓ Port {port} opened successfully")
            return True
    except PermissionError:
        print(f"✗ Port {port} - Access denied (port may be in use by another program)")
        return False
    except serial.SerialException as e:
        print(f"✗ Port {port} - Serial error: {e}")
        return False
    except Exception as e:
        print(f"✗ Port {port} - Error: {e}")
        return False

class OPCSender:
    def __init__(self, port: str, baudrate: int = 115200):
        """Initialize OPC sender with serial connection"""
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.frame_count = 0

    def connect(self):
        """Connect to the serial port"""
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=1)
            time.sleep(2)  # Wait for connection to stabilize
            print(f"Connected to {self.port} at {self.baudrate} baud")
            return True
        except Exception as e:
            print(f"Failed to connect to {self.port}: {e}")
            return False

    def disconnect(self):
        """Disconnect from serial port"""
        if self.serial and self.serial.is_open:
            self.serial.close()
            print("Disconnected")

    def send_frame(self, pixels: List[Tuple[int, int, int]]):
        """Send OPC frame with RGB pixel data"""
        if not self.serial or not self.serial.is_open:
            return False

        # Ensure we have the right number of pixels
        if len(pixels) != NUM_PIXELS:
            print(f"Warning: Expected {NUM_PIXELS} pixels, got {len(pixels)}")
            # Pad or truncate as needed
            if len(pixels) < NUM_PIXELS:
                pixels.extend([(0, 0, 0)] * (NUM_PIXELS - len(pixels)))
            else:
                pixels = pixels[:NUM_PIXELS]

        # Build OPC frame
        # Header: [channel][command][length_hi][length_lo]
        pixel_data_length = NUM_PIXELS * 3
        header = struct.pack('>BBH', OPC_CHANNEL, OPC_COMMAND_SET_PIXELS, pixel_data_length)

        # Convert pixel data to bytes
        pixel_bytes = bytearray()
        for r, g, b in pixels:
            pixel_bytes.extend([r & 0xFF, g & 0xFF, b & 0xFF])

        # Send complete frame
        frame = header + pixel_bytes
        self.serial.write(frame)
        self.serial.flush()

        self.frame_count += 1
        return True

    def send_black_frame(self):
        """Send a frame of all black pixels to clear the display"""
        if not self.serial or not self.serial.is_open:
            return False

        print("Clearing display (sending all black)...")
        black_pixels = [(0, 0, 0)] * NUM_PIXELS
        return self.send_frame(black_pixels)

class PatternGenerator:
    """Generate various demo patterns for the LED matrix"""

    def __init__(self, width: int, height: int):
        self.width = width
        self.height = height
        self.time = 0

    def xy_to_index(self, x: int, y: int) -> int:
        """Convert x,y coordinates to linear pixel index"""
        return y * self.width + x

    def hsv_to_rgb(self, h: float, s: float, v: float) -> Tuple[int, int, int]:
        """Convert HSV to RGB (0-255 range)"""
        import colorsys
        r, g, b = colorsys.hsv_to_rgb(h, s, v)
        return int(r * 255), int(g * 255), int(b * 255)

    def solid_color(self, r: int = 255, g: int = 0, b: int = 0) -> List[Tuple[int, int, int]]:
        """Generate solid color pattern"""
        return [(r, g, b)] * (self.width * self.height)

    def rainbow_horizontal(self) -> List[Tuple[int, int, int]]:
        """Generate horizontal rainbow pattern"""
        pixels = []
        for y in range(self.height):
            for x in range(self.width):
                hue = (x / self.width + self.time * 0.01) % 1.0
                r, g, b = self.hsv_to_rgb(hue, 1.0, 1.0)
                pixels.append((r, g, b))
        return pixels

    def rainbow_vertical(self) -> List[Tuple[int, int, int]]:
        """Generate vertical rainbow pattern"""
        pixels = []
        for y in range(self.height):
            for x in range(self.width):
                hue = (y / self.height + self.time * 0.01) % 1.0
                r, g, b = self.hsv_to_rgb(hue, 1.0, 1.0)
                pixels.append((r, g, b))
        return pixels

    def rainbow_diagonal(self) -> List[Tuple[int, int, int]]:
        """Generate diagonal rainbow pattern"""
        pixels = []
        for y in range(self.height):
            for x in range(self.width):
                hue = ((x + y) / (self.width + self.height) + self.time * 0.01) % 1.0
                r, g, b = self.hsv_to_rgb(hue, 1.0, 1.0)
                pixels.append((r, g, b))
        return pixels

    def moving_wave(self) -> List[Tuple[int, int, int]]:
        """Generate moving sine wave pattern"""
        pixels = []
        for y in range(self.height):
            for x in range(self.width):
                # Create wave based on x position and time
                wave = math.sin((x / self.width * 4 * math.pi) + (self.time * 0.1))
                intensity = int((wave + 1) * 127.5)  # Convert -1,1 to 0,255

                # Create color based on y position
                hue = y / self.height
                r, g, b = self.hsv_to_rgb(hue, 1.0, intensity / 255.0)
                pixels.append((r, g, b))
        return pixels

    def plasma(self) -> List[Tuple[int, int, int]]:
        """Generate plasma effect"""
        pixels = []
        for y in range(self.height):
            for x in range(self.width):
                # Normalized coordinates
                nx = x / self.width
                ny = y / self.height

                # Plasma calculation
                v = math.sin(nx * 10 + self.time * 0.1)
                v += math.sin(ny * 10 + self.time * 0.15)
                v += math.sin((nx + ny) * 10 + self.time * 0.12)
                v += math.sin(math.sqrt(nx * nx + ny * ny) * 10 + self.time * 0.08)

                # Convert to color
                hue = (v + 4) / 8  # Normalize to 0-1
                r, g, b = self.hsv_to_rgb(hue % 1.0, 1.0, 1.0)
                pixels.append((r, g, b))
        return pixels

    def checkerboard(self, size: int = 8) -> List[Tuple[int, int, int]]:
        """Generate animated checkerboard pattern"""
        pixels = []
        offset = int(self.time * 0.5) % (size * 2)

        for y in range(self.height):
            for x in range(self.width):
                checker_x = (x + offset) // size
                checker_y = y // size

                if (checker_x + checker_y) % 2 == 0:
                    hue = (self.time * 0.01) % 1.0
                    r, g, b = self.hsv_to_rgb(hue, 1.0, 1.0)
                else:
                    r, g, b = 0, 0, 0

                pixels.append((r, g, b))
        return pixels

    def update(self):
        """Update animation time"""
        self.time += 1

def main():
    # Parse command line arguments
    if len(sys.argv) > 1:
        if sys.argv[1] in ["--scan", "-s", "scan"]:
            # Just scan for ports and exit
            print("Scanning for available serial ports...\n")
            find_teensy_ports()

            print("\nTesting port connections:")
            ports = serial.tools.list_ports.comports()
            for port in ports:
                test_port_connection(port.device)
            return
        else:
            port = sys.argv[1]
    else:
        # Auto-detect port
        print("No port specified, scanning for Teensy...\n")
        auto_port = find_teensy_ports()
        if auto_port:
            port = auto_port
            print(f"\nAuto-detected port: {port}")
            if not test_port_connection(port):
                print("Auto-detected port failed connection test. Please specify port manually.")
                print("Use: python artnet-to-serial-sender.py --scan to see all ports")
                return
        else:
            print("No ports found. Make sure Teensy is connected.")
            print("Use: python artnet-to-serial-sender.py --scan to see all ports")
            return

    pattern_name = sys.argv[2] if len(sys.argv) > 2 else "rainbow"

    print(f"\nOPC Serial Sender")
    print(f"Matrix: {MATRIX_WIDTH}x{MATRIX_HEIGHT} ({NUM_PIXELS} pixels)")
    print(f"Port: {port}")
    print(f"Pattern: {pattern_name}")
    print("Press Ctrl+C to stop\n")

    # Initialize
    sender = OPCSender(port)
    pattern_gen = PatternGenerator(MATRIX_WIDTH, MATRIX_HEIGHT)

    # Connect
    if not sender.connect():
        print("\nConnection failed. Try:")
        print("  python artnet-to-serial-sender.py --scan")
        print("  python artnet-to-serial-sender.py COMx  (replace x with correct number)")
        return

    try:
        while True:
            # Generate pattern based on name
            if pattern_name == "solid":
                pixels = pattern_gen.solid_color(255, 0, 0)  # Red
            elif pattern_name == "rainbow":
                pixels = pattern_gen.rainbow_horizontal()
            elif pattern_name == "rainbow_v":
                pixels = pattern_gen.rainbow_vertical()
            elif pattern_name == "rainbow_d":
                pixels = pattern_gen.rainbow_diagonal()
            elif pattern_name == "wave":
                pixels = pattern_gen.moving_wave()
            elif pattern_name == "plasma":
                pixels = pattern_gen.plasma()
            elif pattern_name == "checker":
                pixels = pattern_gen.checkerboard()
            else:
                print(f"Unknown pattern: {pattern_name}")
                print("Available patterns: solid, rainbow, rainbow_v, rainbow_d, wave, plasma, checker")
                break

            # Send frame
            if sender.send_frame(pixels):
                if sender.frame_count % 100 == 0:
                    print(f"Sent {sender.frame_count} frames")

            # Update animation
            pattern_gen.update()

            # Control frame rate (30 FPS)
            time.sleep(1/30)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        # Clear the display before disconnecting
        sender.send_black_frame()
        sender.disconnect()

if __name__ == "__main__":
    main()