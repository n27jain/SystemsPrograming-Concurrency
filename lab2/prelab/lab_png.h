#pragma once

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <errno.h>

#include "crc.h"
#include "zutil.h"

#define PNG_SIG_SIZE    8 /* number of bytes of png image signature data */
#define CHUNK_LEN_SIZE  4 /* chunk length field size in bytes */          
#define CHUNK_TYPE_SIZE 4 /* chunk type field size in bytes */
#define CHUNK_CRC_SIZE  4 /* chunk CRC field size in bytes */
#define DATA_IHDR_SIZE 13 /* IHDR chunk data field size */

extern const int CHUNK_SIZE;

typedef unsigned int  U32;

typedef struct chunk {
    U32 length;  /* length of data in the chunk, network byte order */
    U8  type[4]; /* chunk type */
    U8  *p_data; /* pointer to location where the actual data are */
    U32 crc;     /* CRC field  */
} *chunk_p;

struct data_IHDR {// IHDR chunk data 
    U32 width;        /* width in pixels, big endian   */
    U32 height;       /* height in pixels, big endian  */
    U8  bit_depth;    /* num of bits per sample or per palette index.
                         valid values are: 1, 2, 4, 8, 16 */
    U8  color_type;   /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                         =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8  compression;  /* only method 0 is defined for now */
    U8  filter;       /* only method 0 is defined for now */
    U8  interlace;    /* =0: no interlace; =1: Adam7 interlace */
};

struct PNG {
    struct data_IHDR IHDR;
	  int idat_length;
    struct chunk* p_IDAT;
};

int is_png(U8* buf, size_t n);
int get_png_height(struct data_IHDR* buf);
int get_png_width(struct data_IHDR* buf);
int get_png_data_IHDR(struct chunk* chk, struct data_IHDR** out, FILE* fp, long offset, int whence);
int get_chunk_from_file(struct chunk* out, FILE* fp, long offset, int whence);
int get_chunk_from_memory(U8* mem, int length, struct chunk* out);
U32 get_chunk_length(struct chunk* chk);
void free_chunk(struct chunk* chk);
int calculate_chunk_crc(struct chunk* chk, unsigned long* result);
int check_png_validity(const char* file);
int load_png_from_file(const char* file, struct PNG* png);
int load_png_from_memory(U8* mem, int length, struct PNG* png);
void free_png_data(struct PNG* png);

int write_png_header(FILE* fp);
int write_png_chunk(FILE* fp, struct chunk* chk);
int write_png_file(const char* filename, struct PNG* png);
