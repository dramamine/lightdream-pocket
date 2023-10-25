
import cv2
import math
import os

output_file_path  = "./output.bin"

# make sure these match on the Teensy
# i.e. LEDs per universe
WIDTH = 170*3
# i.e. number of outputs used by the Teensy
HEIGHT = 8
FPS = 40.0

def to_array(rows):
  res = []
  for row in rows:
    for col in row:
      res.append(col[0])
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
    assert self.width == 512

    self.height = int(self.vid.get(cv2.CAP_PROP_FRAME_HEIGHT))
    assert self.height % HEIGHT == 0, f"Video has wrong height {self.height} for specified # of outputs {HEIGHT}"

    self.rows_per_row = int(self.height / HEIGHT)



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
      # print("that was all the frames. end of song probably? framecount={}".format(self.framecount))
      self.ended = True
      return None

class Video2SDCard:
  def __init__(self, output_file):
    self.output_file = output_file
    self.sp = SequencePlayer()
    sequencePath = os.path.join('{}.mp4'.format("340x4-example"))
    # sequencePath = os.path.join('embedded', '{}.mp4'.format("red-green-wipe-veryslow-30s"))
    self.sp.play(sequencePath)
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
      start = 2 * i
      end = start + self.sp.rows_per_row
      rows = frame[start:end]
      row_array = to_array(rows)[0:WIDTH*3]
      data = bytearray( row_array )
      self.output_file.write(data)

  def write_frames(self):
    while(True):
      frame = self.sp.read_frame()
      if frame is None:
        break
      self.write_header()
      self.write_frame(frame)


if __name__ == "__main__":
  output_file = open(output_file_path, "wb")
  x = Video2SDCard(output_file)

  x.write_frames()
  x.write_eof_header()
  output_file.close()
