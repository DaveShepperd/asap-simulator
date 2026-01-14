LIB_BASE = /usr/local/asap-gca/cross_clib/
CLIB = $(LIB_BASE)/crtl.lib
CFLAGS = -I $(LIB_BASE)/include -I.
CC = gca
MACAS = macas
MACPP = macpp
LLF = llf
MIXIT = mixit
TARGET = basic.img
ECHO = /usr/bin/echo -e

.SILENT:

default: $(TARGET) f.img

$(TARGET) : basic.hex basic.mix Makefile
	$(ECHO) "\tMixit basic ..."
	$(MIXIT) basic.mix

f.img : f.hex Makefile
	$(ECHO) "\tMixit f ..."
	$(MIXIT) f.mix

f.hex : f.ol root.ol 
	$(ECHO) "\tLinking to $@ ..."
	$(LLF) -out=$@ -map -stb=f.stb -opt=f.opt

f.ol : f.s
f.s : f.c
i_fputn.ol : i_fputn.s
i_fputn.s : i_fputn.c

basic.hex : basic.ol root.ol basic.opt $(CLIB) Makefile
	$(ECHO) -e "\tLinking to $@ ..."
	$(LLF) -out=$@ -map -stb=basic.stb -opt=basic.opt

%.ol: %.s
	$(ECHO) -e "\tAssembling $< ..."
	$(MACAS) -out=$@ -lis $<

%.ol: %.mac
	$(ECHO) -e "\tAssembling $< ..."
	$(MACAS) -out=$@ -lis $<

%.s: %.c
	$(ECHO) -e "\tCompiling $< ..."
	$(CC) -S $(CFLAGS) -o $@ $<

%.mpp: %.mac
	$(ECHO) -e "\tMaking $@ ..."
	$(MACPP) -out=$@ -out=$(patsubst %.mpp,%.h,$@) $<

syscalls.h: syscalls.mpp
syscalls.mpp: syscalls.mac
basic.s : basic.c syscalls.h Makefile
basic.ol : basic.s
root.ol : root.mac syscalls.mpp

clean:
	rm -f basic.ol basic.s basic.lis basic.map basic.hex basic.img syscalls.mpp syscalls.h f.hex f.map f.ol f.s f.img f.lis root.lis root.ol i_fputn.ol i_fputn.s i_fputn.lis
