# asap-sim

This is a simple and dumb ASAP simulator (ASAP = Atari Simplified Architecture Processor; see below).

Not very useful to anybody but me and then only as an exerciser for my brain cells. It was interesting to make.
Still might not be completely right. Certainly not as effecient as it could be, but who cares.

As it happens, I had a bunch of old ASAP code lying around and I wanted to see if it could be made to work again.
I have no actual hardware and don't want any so I thought a simulator might be interesting. Add to that I recently
(2025) found the old version of gcc we had hacked up to make ASAP code and spent some time getting that to build and work
under the more recent Ubuntu. Find that version of gcc [HERE](https://github.com/DaveShepperd/gca.git)

I gave Claude the task to write a **very** simple Basic interpreter which was just something simple I could use to
prove the simulator worked (or not). The Makefile builds a basic.img file which is what you hand to asap-sim.

If you want to build and use this, you first need to build the 
[MACXX](https://github.com/daveshepperd/macxx.git),
[LLF](https://github.com/daveshepperd/llf.git),
[MIXIT](https://github.com/daveshepperd/mixit.git),
[LIBR](https://github.com/daveshepperd/libr.git),
[CROSS_CLIB](https://github.com/daveshepperd/cross_clib_asap.git),
and [GCA](https://github.com/daveshepperd/gca.git) toolchains.

Edit the Makefile as necessary to point to where you have the tools and libraries. Edit the basic.opt file and change
the **library** to make it point to where you put the crtl.lib files. Type make. If everything works properly,

asap-sim basic.img

should bring up the simple Basic interpreter. The Makefile also builds the Basic interpreter in Ubuntu Linux too for
comparison and/or testing purposes.

### History
The development of the ASAP began in the very late 1980's with final silicon (Rev 3) arriving early 1990's. This was the
period when many companies were experimenting with making and using RISC (Reduced Instruction Set Computer) CPU's.
I believe the idea was for Atari Coin-op to make a cheap 32 bit replacement for esentially the 6502. But being a
CPU custom to Coin-op, it would go a long way to obviate the need for more complex security chips. There were only 
a couple of games made with the chip but it was used by the hundred's in engineering in a device akin to a Raspberry Pi
developed specifically and exclusively for Coin-op engineers. This engineering device was used up until the place
closed up shop in early 2003, then I continued to use one for a few months afterwards under contract.
