# Hardware Gameboy Emulator

A Gameboy Emulator for CPU/FPGA/GPU(coming soon), it can be used as a deep reinforcement learning environment.

This project is based on the work of Kamil Rocki:

- https://towardsdatascience.com/a-gameboy-supercomputer-33a6955a79a4
- http://olab.is.s.u-tokyo.ac.jp/~kamil.rocki/nintendo/

The original C code for gameboy emulator has been modified to remove functions not supported by FPGA HLS. 

The emulator has been tested on x86_64 CPU, ARM CPU, and PYNQ-Z2 FPGA, and the ouput is GIF animation. The test code is written in Python 3. On CPU the python test code uses CFFI to call the compiled dynamic library. On PYNQ the python code uses overlay to communicate with FPGA. 

To run the tests you may need to install several python packages:

- pip3 install cffi array2gif scipy numpy 

Run on CPU
===========

1. To generate dynamic library, just run: make
2. Run: python3 gameboy.py --rom mario.gb --write_gif_every 1
3. GIF files will be generated

Run on FPGA (PYNQ-Z2)
=====================

1. Connect to pynq via SSH, change user to root
2. Run: python3 gameboy_pynq.py --rom mario.gb --write_gif_every 1
3. GIF files will be generated

Compile for FPGA (PYNQ-Z2)
==========================

1. create a Vivado HLS project, import the c++ code file, compile it and export an IP
2. create a Vivado project, import the IP, then create a new block design. In the block design connect both the AXI slave and master ports to ZYNQ. Then generate a bitstream, export the block design as tcl, and find the hwh file in project folder.

