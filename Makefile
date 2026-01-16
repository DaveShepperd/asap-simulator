GCA_LIB_BASE = /usr/local/asap-gca/cross_clib/
GCA_CLIB = $(GCA_LIB_BASE)/crtl.lib
GCA_OPT = -O
GCA_CFLAGS = -I $(GCA_LIB_BASE)/include -I. $(GCA_OPT)
GCC_OPT = -O2
GCC_DBG = #-g
GCC_CFLAGS = $(GCC_OPT) $(GCC_DBG) -Wall
GCA = gca
GCC = gcc
MACAS = macas
MACPP = macpp
LLF = llf
MIXIT = mixit
TARGET = basic.img
ECHO = /usr/bin/echo -e

BASIC_CFILES     = basic.c
BASIC_OBJS = $(patsubst %.c,%.o,$(BASIC_CFILES))

ASAP_SIM_CPPFILES  = main.cpp
ASAP_SIM_CPPFILES += asapExecute.cpp
ASAP_SIM_CPPFILES += get_stb.cpp
ASAP_SIM_CPPFILES += lclreadline.cpp
ASAP_SIM_CPPFILES += qa.cpp
ASAP_SIM_OBJS  = $(patsubst %.cpp,%.o,$(ASAP_SIM_CPPFILES))

.SILENT:

default: $(TARGET) asap-sim # f.img

basic : basic.o Makefile

asap-sim: $(ASAP_SIM_OBJS) Makefile
	$(ECHO) "\tLinking to $@ ..."
	$(GCC) -o $@ $(ASAP_SIM_OBJS)

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

%.o: %.c
	$(ECHO) -e "\tCompiling $< ..."
	$(GCC) $(GCC_CFLAGS) -c $<
	
%.o: %.cpp
	$(ECHO) -e "\tCompiling $< ..."
	$(GCC) $(GCC_CFLAGS) -c $<
	
%.ol: %.s
	$(ECHO) -e "\tAssembling $< ..."
	$(MACAS) -out=$@ -lis $<

%.ol: %.mac
	$(ECHO) -e "\tAssembling $< ..."
	$(MACAS) -out=$@ -lis $<

%.s: %.c
	$(ECHO) -e "\tCompiling $< ..."
	$(GCA) -S $(GCA_CFLAGS) -o $@ $<

%.mpp: %.mac
	$(ECHO) -e "\tMaking $@ ..."
	$(MACPP) -out=$@ -out=$(patsubst %.mpp,%.h,$@) $<

syscalls.h: syscalls.mpp
syscalls.mpp: syscalls.mac
basic.s : basic.c syscalls.h Makefile
basic.ol : basic.s
root.ol : root.mac syscalls.mpp
asapExecute.o : asapExecute.cpp syscalls.h

clean:
	rm -f *.img *.hex *.s *.lis *.ol *.map *.o
	rm -f syscalls.mpp syscalls.h
