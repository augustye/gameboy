# Hardware Gameboy Emulator

A Gameboy Emulator for CPU/FPGA/GPU(coming soon), it can be used as a deep reinforcement learning environment.

This project is based on the work of Kamil Rocki:

- https://towardsdatascience.com/a-gameboy-supercomputer-33a6955a79a4
- http://olab.is.s.u-tokyo.ac.jp/~kamil.rocki/nintendo/

The original C code for gameboy emulator has been modified to remove functions not supported by FPGA HLS. 

The emulator has been tested on x86_64 CPU, Arm CPU, and PYNQ-Z2 FPGA, and the ouput is GIF animations. The test code is writen in Python 3. On CPU the python code use CFFI to call the compiled dynamic library. On PYNQ the python code use overlay to comminicate with FPGA. 

To run the tests you may need to install several python packages:

- pip3 install cffi array2gif scipy numpy 

To run on CPU
=============

1. make
2. python3 gameboy.py --rom mario.gb --write_gif_every 1

To run on FPGA (PYNQ-Z2)
=========================

python3 gameboy_pynq.py --rom mario.gb --write_gif_every 1



