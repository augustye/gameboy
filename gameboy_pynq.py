import pynq
import time, argparse
from array2gif import write_gif
from scipy.misc import imresize
import random
import numpy as np

write_cart      = 1
get_screen      = 2
reset           = 3
next_frame_skip = 4
set_keys        = 5

class gameboy_overlay():
    def __init__(self):
        self.overlay = pynq.Overlay("../gameboy_overlay2.bit")
    
    def interface(self, cmd, data, ptr):
        self.overlay.interface_0.register_map.cmd  = 0
        self.overlay.interface_0.register_map.ptr  = ptr
        self.overlay.interface_0.register_map.data = data
        self.overlay.interface_0.register_map.cmd  = cmd
    
# init GB subsystem
def init(rom_path):
  _xlnk = pynq.Xlnk()
  _gb = gameboy_overlay()
  _cart = _xlnk.cma_array(shape=(65536,), dtype=np.uint8)
  _frame = _xlnk.cma_array(shape=(144,160,3), dtype=np.uint8)

  np.copyto(_cart, np.fromfile(rom_path, dtype=np.uint8))

  _gb.interface(write_cart, write_cart, _cart.physical_address)
  _gb.interface(reset, reset, _frame.physical_address)

  return _gb, _frame, _frame.physical_address


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
    gb, frame, frame_ptr = init(args.rom)

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
      gb.interface(get_screen, get_screen, frame_ptr)

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
      gb.interface(set_keys, actions_hex[a], frame_ptr)
      gb.interface(next_frame_skip, args.skipframes, frame_ptr)

      # terminate?
      if frames > args.framelimit:
        break

      frames += args.skipframes
