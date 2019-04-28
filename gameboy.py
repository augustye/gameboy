from cffi import FFI
import time, argparse
from array2gif import write_gif
from scipy.misc import imresize
import random
import numpy as np

class gameboy_interface():

    def __init__(self):
      ffi = FFI()
      ffi.cdef("void interface(uint8_t cmd, uint32_t data, uint8_t* ptr, uint32_t* result);")
      self.dl = ffi.dlopen("./gameboy.so"); 
      self.frame = np.zeros([144,160,3], dtype=np.uint8)
      self.frame_ptr = ffi.cast("uint8_t *", self.frame.ctypes.data)
      self.result = np.zeros(1, dtype=np.uint32)
      self.result_ptr = ffi.cast("uint32_t *", self.result.ctypes.data)

    def interface(self, cmd=0, data=0, ptr=0):  
      self.dl.interface(cmd, data, ptr or self.frame_ptr, self.result_ptr)

    def write_cart(self, rom_path):
      rom = open(rom_path, 'rb').read()
      self.interface(cmd=1, ptr=rom)

    def get_screen(self):
      self.interface(cmd=2, ptr=self.frame_ptr)
      return self.frame

    def reset(self):
      self.interface(cmd=3)

    def next_frame_skip(self, data):
      self.interface(cmd=4, data=data)

    def set_keys(self, data):
      self.interface(cmd=5, data=data)

# parse commandline args
def get_args():
    parser = argparse.ArgumentParser(description=None)
    parser.add_argument('--processes', default=1, type=int, help='number of processes to train with')
    parser.add_argument('--framelimit', default=1000000, type=int, help='frame limit')
    parser.add_argument('--skipframes', default=8, type=int, help='frame increment, def=1')
    parser.add_argument('--rom', default='./wario_walking.gb', type=str, help='path to rom')
    parser.add_argument('--write_gif_every', default=60, type=int, help='write to gif every n secs')
    parser.add_argument('--write_gif_duration', default=50, type=int, help='number of frames to write')
    return parser.parse_args()

if __name__ == "__main__":

    args = get_args()
    imgs,frames,episodes=[],0,0
    gb = gameboy_interface()
    gb.write_cart(args.rom)
    gb.reset()

    actions_hex = [
      0x00, #nop
      0x01, #select
      0x02, #start
      0x04, #a
      0x08, #b
      0x10, #left
      0x20, #right
      0x40, #down
      0x80, #up
      0x14, #a + left
      0x24, #a + right
      0x44, #a + down
      0x84, #a + up
      0x18, #b + left
      0x28, #b + right
      0x48, #b + down
      0x88  #b + up
    ]

    t0 = last_time = time.time()
    imgs, frames_to_write = [], 0

    # main loop
    while True:

      # process a frame
      frame = gb.get_screen()

      # checkpoint?
      if (time.time() - last_time) > args.write_gif_every:
        elapsed = time.strftime("%Hh %Mm %Ss", time.gmtime(time.time() - t0))
        print('time: {}, frames {:.2f}M'.format(elapsed, frames/1e6))
        frames_to_write = args.write_gif_duration;
        last_time = time.time()

      # write frames
      if frames_to_write > 0:
          fr = frame.copy()
          imgs.append(np.rot90(np.fliplr(fr)))
          frames_to_write -= 1

      # write to gif?
      if len(imgs) == args.write_gif_duration:
        write_gif(imgs, '{}_{}.gif'.format(args.rom, frames),fps=10)
        imgs=[]

      # decide on the action
      a = random.randint(0,len(actions_hex)-1)
      gb.set_keys(actions_hex[a])
      gb.next_frame_skip(args.skipframes)

      # terminate?
      if frames > args.framelimit:
        break

      frames += args.skipframes
