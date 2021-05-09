# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS = -Wall -g -ggdb3 -std=c99 -D_DEFAULT_SOURCE  # compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS = -lz -lm  # link with libz and math.h

# For students 
LIB_UTIL = zutil.o crc.o
SRCS   = pnginfo.c crc.c zutil.c lab_png.c lab_file.c
OBJS   = lab_png.o lab_file.o $(LIB_UTIL) 

TARGETS= pnginfo findpng catpng

all: ${TARGETS}

pnginfo: pnginfo.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

findpng: findpng.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

catpng: catpng.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS) 
