# Makefile, ECE252  
# Yiqing Huang

CC = gcc       # compiler
CFLAGS_XML2 = $(shell xml2-config --cflags)
CFLAGS_CURL = $(shell curl-config --cflags)
CFLAGS = -Wall $(CFLAGS_XML2) $(CFLAGS_CURL) -g -ggdb3 -std=c99 -D_DEFAULT_SOURCE  # compilation flags
LD = gcc       # linker
LDFLAGS = -g   # debugging symbols in build
LDLIBS_XML2 = $(shell xml2-config --libs)
LDLIBS_CURL = $(shell curl-config --libs)
LDLIBS = -lz -lm $(LDLIBS_XML2) $(LDLIBS_CURL) -pthread  # link with libz and math.h

# For students 
LIB_UTIL = zutil.o crc.o
SRCS   = crc.c zutil.c lab_net.c lab_png.c lab_file.c
OBJS   = lab_net.o lab_png.o lab_file.o $(LIB_UTIL) 

TARGETS= pnginfo findpng findpng2 catpng paster paster2

all: ${TARGETS}

.PHONY: debug
debug: CFLAGS += -DDEBUG_1
debug: ${TARGETS}

pnginfo: pnginfo.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

findpng: findpng.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

findpng2: findpng2.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

catpng: catpng.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

paster: paster.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

paster2: paster2.o $(OBJS)
	$(LD) -o $@ $^ $(LDLIBS) $(LDFLAGS)

%.o: %.c 
	$(CC) $(CFLAGS) -c $< 

%.d: %.c
	gcc -MM -MF $@ $<

-include $(SRCS:.c=.d)

.PHONY: clean
clean:
	rm -f *.d *.o $(TARGETS) *.png *.html *.txt 
