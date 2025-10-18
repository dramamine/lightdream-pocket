#!/usr/bin/env python3
"""
Artnet to OPC Serial Bridge
Receives Artnet data from Resolume and forwards it via OPC over Serial to Teensy

Usage:
    python artnet-to-serial-sender.py [COM_PORT]

Examples:
    python artnet-to-serial-sender.py COM3
    python artnet-to-serial-sender.py /dev/ttyACM0
"""

import serial
import serial.tools.list_ports
import socket
import time
import sys
import struct
import threading
from typing import Tuple, List, Dict

# Matrix configuration (must match Teensy code)
MATRIX_WIDTH = 64
MATRIX_HEIGHT = 64*3  # 192 total height
NUM_PIXELS = MATRIX_WIDTH * MATRIX_HEIGHT

# Artnet configuration
ARTNET_PORT = 6454
ARTNET_HEADER = b"Art-Net\x00"
ARTNET_OPCODE_DMX = 0x5000
NUM_UNIVERSES = 73
LEDS_PER_UNIVERSE = 170

# OPC Protocol constants
OPC_CHANNEL = 0
OPC_COMMAND_SET_PIXELS = 0
OPC_HEADER_SIZE = 4

class ArtnetReceiver:
    """Receives Artnet DMX data and assembles complete frames"""

    def __init__(self):
        self.socket = None
        self.universe_data = {}  # Dict[int, bytes] - universe_id -> dmx_data
        self.frame_complete = threading.Event()
        self.running = False
        self.thread = None
        self.frame_count = 0
        self.last_frame_time = 0

    def start(self, bind_ip: str = "0.0.0.0"):
        """Start listening for Artnet data"""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self.socket.bind((bind_ip, ARTNET_PORT))
            self.running = True

            self.thread = threading.Thread(target=self._receive_loop, daemon=True)
            self.thread.start()

            print(f"Artnet receiver started on {bind_ip}:{ARTNET_PORT}")
            return True

        except Exception as e:
            print(f"Failed to start Artnet receiver: {e}")
            return False

    def stop(self):
        """Stop the Artnet receiver"""
        self.running = False
        if self.socket:
            self.socket.close()
        if self.thread:
            self.thread.join()

    def _receive_loop(self):
        """Main receive loop running in separate thread"""
        while self.running:
            try:
                data, addr = self.socket.recvfrom(1024)
                self._parse_artnet_packet(data)
            except Exception as e:
                if self.running:  # Only print error if we're still supposed to be running
                    print(f"Artnet receive error: {e}")

    def _parse_artnet_packet(self, data: bytes):
        """Parse incoming Artnet packet"""
        if len(data) < 18:  # Minimum Artnet packet size
            return

        # Check Artnet header
        if data[0:8] != ARTNET_HEADER:
            return

        # Check opcode (little endian)
        opcode = struct.unpack('<H', data[8:10])[0]
        if opcode != ARTNET_OPCODE_DMX:
            return

        # Extract universe (little endian)
        universe = struct.unpack('<H', data[14:16])[0]

        # Extract DMX data length (big endian)
        length = struct.unpack('>H', data[16:18])[0]

        # Extract DMX data
        dmx_data = data[18:18+length]

        if universe < NUM_UNIVERSES:
            self.universe_data[universe] = dmx_data

            # Check if we have all universes for a complete frame
            if len(self.universe_data) >= NUM_UNIVERSES:
                self.frame_complete.set()

    def wait_for_frame(self, timeout: float = 1.0) -> bool:
        """Wait for a complete frame to be received"""
        return self.frame_complete.wait(timeout)

    def get_frame_data(self) -> List[Tuple[int, int, int]]:
        """Get complete frame as RGB pixel list"""
        pixels = []

        # Process universes in order
        for universe in range(NUM_UNIVERSES):
            if universe in self.universe_data:
                dmx_data = self.universe_data[universe]

                # Convert DMX data to RGB pixels (3 channels per pixel)
                for i in range(0, min(len(dmx_data), LEDS_PER_UNIVERSE * 3), 3):
                    if i + 2 < len(dmx_data):
                        r = dmx_data[i]
                        g = dmx_data[i + 1]
                        b = dmx_data[i + 2]
                        pixels.append((r, g, b))

        # Clear frame data and reset event
        self.universe_data.clear()
        self.frame_complete.clear()
        self.frame_count += 1

        # Pad or truncate to exact pixel count
        while len(pixels) < NUM_PIXELS:
            pixels.append((0, 0, 0))
        pixels = pixels[:NUM_PIXELS]

        return pixels

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

    print(f"\nArtnet to OPC Serial Bridge")
    print(f"Matrix: {MATRIX_WIDTH}x{MATRIX_HEIGHT} ({NUM_PIXELS} pixels)")
    print(f"Serial Port: {port}")
    print(f"Artnet: {NUM_UNIVERSES} universes, {LEDS_PER_UNIVERSE} LEDs/universe")
    print(f"Listening on port {ARTNET_PORT}")
    print("Press Ctrl+C to stop\n")

    # Initialize components
    artnet_receiver = ArtnetReceiver()
    opc_sender = OPCSender(port)

    # Connect serial
    if not opc_sender.connect():
        print("\nSerial connection failed. Try:")
        print("  python artnet-to-serial-sender.py --scan")
        print("  python artnet-to-serial-sender.py COMx  (replace x with correct number)")
        return

    # Start Artnet receiver
    if not artnet_receiver.start():
        print("Failed to start Artnet receiver")
        opc_sender.disconnect()
        return

    try:
        print("Bridge active - waiting for Artnet data from Resolume...")
        frames_sent = 0
        last_status_time = time.time()

        while True:
            # Wait for complete Artnet frame (all 73 universes)
            if artnet_receiver.wait_for_frame(timeout=1.0):
                # Get pixel data from Artnet
                pixels = artnet_receiver.get_frame_data()

                # Send via OPC over Serial
                if opc_sender.send_frame(pixels):
                    frames_sent += 1

                    # Status update every 5 seconds
                    if time.time() - last_status_time > 5.0:
                        print(f"Frames bridged: {frames_sent} (Artnet: {artnet_receiver.frame_count})")
                        last_status_time = time.time()
            else:
                # Timeout - no Artnet data received
                # print("No Artnet data received in 1 second...")
                pass

    except KeyboardInterrupt:
        print("\nStopping bridge...")
    finally:
        # Clean shutdown
        artnet_receiver.stop()
        opc_sender.send_black_frame()
        opc_sender.disconnect()

if __name__ == "__main__":
    main()