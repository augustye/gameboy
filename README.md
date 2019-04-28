# gameboy

Gameboy Emulator for CPU/FPGA/GPU(coming soon), it can be used as a deep reinforcement learning environment.

This project is based on the work of Kamil Rocki:

- https://towardsdatascience.com/a-gameboy-supercomputer-33a6955a79a4
- http://olab.is.s.u-tokyo.ac.jp/~kamil.rocki/nintendo/

The original C code has been modified for FPGA HLS. Currently tested on x86_64 CPU, Arm CPU, and PYNQ-Z2 FPGA, and the ouput is GIF animations.

To run on cpu
=============

1. make
2. python3 gameboy.py --rom mario.gb --write_gif_every 1

To run on PYNQ-Z2
=================

python3 gameboy_pynq.py --rom mario.gb --write_gif_every 1



