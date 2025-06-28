import cv2
import math
import argparse
import time
import re
USE_GRB = False

parser = argparse.ArgumentParser(description="Convert video to SD card format for LEDs.")
parser.add_argument("--sequence_path", type=str, default="./340x8-noise-pattern.mp4", help="Path to the input video file.")
parser.add_argument("--use_dimensions", type=str, default="170x8", help="Dimensions of the LED matrix in format WIDTHxHEIGHT.")
parser.add_argument("--fps", type=float, default=40.0, help="Frames per second for the output video.")
parser.add_argument("--output_file_path", type=str, default="L:/output.bin", help="Path to the output binary file.")
args = parser.parse_args()

#TODO read dimensions from filename, if available


sequence_path = args.sequence_path
output_file_path = args.output_file_path


if args.use_dimensions:
    use_dimensions = args.use_dimensions
else:
  match = re.match(r"./(\d+)x(\d+).*", args.sequence_path)
  if match:
    use_dimensions = f"{match.group(1)}x{match.group(2)}"
  else:
    use_dimensions = "170x8"

width_str, height_str = use_dimensions.split('x')
WIDTH = int(width_str)
HEIGHT = int(height_str)

print(f"Using dimensions: WIDTH={WIDTH}, HEIGHT={HEIGHT}")

# NOTE: make sure the teensy has these same values for width and height
#
# how many LEDs of data do we read from each video row?
# this number is 170 maximum (510 bytes per row / 3 bytes per pixel)
# WIDTH = 170
# i.e. number of outputs used by the Teensy.
# just plan things to use 8 rows and you'll be happy
# HEIGHT = 8
FPS = 40.0

def to_array(rows):
  controversial_max_range = min(170, WIDTH)
  res = []
  for row in rows:
    for i in range(controversial_max_range):
      # render in GRB oder
      if (USE_GRB):
          res.append([
              row[3*i+1][0],
              row[3*i][0],
              row[3*i+2][0]
          ])
      else:
          res.append([
              row[3*i][0],
              row[3*i+1][0],
              row[3*i+2][0]
          ])

  # flatten the array
  res = [item for sublist in res for item in sublist]
  return res


class SequencePlayer:
  def __init__(self, loop=False):
    self.vid = None
    self.path = ""
    self.loop = loop
    self.framecount = 0
    self.delay_frames = 0
    self.delay_frames_left = 0

  def play(self, path):
    self.path = path
    self.vid = cv2.VideoCapture(path)
    self.framecount = 0
    self.ended = False

    assert HEIGHT * 512 > WIDTH * 3, f"For width {WIDTH} you'll need {math.ceil(WIDTH*3 / 512)} universes or greater per output row"

    self.width = int(self.vid.get(cv2.CAP_PROP_FRAME_WIDTH))
    assert self.width == 512, f"Video has wrong width {self.width} (expecting 512)"

    self.height = int(self.vid.get(cv2.CAP_PROP_FRAME_HEIGHT))
    assert self.height % HEIGHT == 0, f"Video has wrong height {self.height} for specified # of outputs {HEIGHT}"

    self.video_rows_per_led_row = int(self.height / HEIGHT)
    print("video rows (universes) per led row:", self.video_rows_per_led_row)



    frames = int(self.vid.get(cv2.CAP_PROP_FRAME_COUNT))
    print("starting sequence: frames={} ({}m{}s)".format(
      frames, math.floor(frames/(FPS*60)), math.floor(frames/FPS) % 60
    ))

  def read_frame(self):
    if self.delay_frames_left > 0:
      self.delay_frames_left -= 1
      return None

    if self.ended:
      return None

    if not self.vid:
      print("need 2 play a video before reading frames")
      return None

    ret,frame = self.vid.read()

    if ret:
      self.framecount += 1

      return frame

    else:
      if (self.loop):
        self.play(self.path)
        return self.read_frame()
      self.ended = True
      return None

class Video2SDCard:
  def __init__(self, output_file):
    self.output_file = output_file
    self.sp = SequencePlayer()
    self.sp.play(sequence_path)
    print(self.sp.width)
    print(self.sp.height)

  def write_header(self):

    size = WIDTH * HEIGHT
    size_0 = size % 256
    size_1 = math.floor(size / 256)
    usec = math.floor(1000000 / FPS)
    usec_0 = usec % 256
    usec_1 = math.floor(usec / 256)

    bytes = bytearray([42, size_0, size_1, usec_0, usec_1])
    self.output_file.write(bytes)

  def write_eof_header(self):
    bytes = bytearray([126, 126, 126, 126, 126])
    self.output_file.write(bytes)


  def write_frame(self, frame):
    for i in range(HEIGHT):
      start = self.sp.video_rows_per_led_row * i
      end = start + self.sp.video_rows_per_led_row
      rows = frame[start:end]

      row_array = to_array(rows)
      data = bytearray( row_array )

      # this can happen if you have the wrong value for video_rows_per_led_row or
      # try to read from a row that doesn't exist
      assert len(data) == WIDTH * 3, f"Wrong number of bytes in row {i}: {len(data)}"

      self.output_file.write(data)

  def write_frames(self):
    while(True):
      frame = self.sp.read_frame()
      if frame is None:
        break
      self.write_header()
      self.write_frame(frame)


if __name__ == "__main__":
  # Create a timer

  start_time = time.time()

  output_file = open(output_file_path, "wb")
  x = Video2SDCard(output_file)

  x.write_frames()
  x.write_eof_header()
  output_file.close()

  end_time = time.time()
  elapsed_time = end_time - start_time
  print(f"Finished writing {x.sp.framecount} frames in {elapsed_time:.2f} seconds")
