#include "lab_file.h"

int main(const int argc, const char * const * argv) {
	if (argc < 2) {
		fprintf(stderr, "error: invalid arguments, expected <file1>, [file2], ...\n");
		return -1;
	}
	int ret = 0;
	int i;
	FILE* outfile = fopen("all.png", "w");
	write_png_header(outfile);
	int outwidth = 0;
	int outheight = 0;
	bool first = true;
	struct chunk idat;
	idat.length = 0;
	idat.p_data = NULL;
	memcpy(idat.type, "IDAT", 4);
	for (i = 1; i < argc; i++) {
		struct PNG png;
		if (load_png_from_file(argv[i], &png) == 0) {
			if (first) {
				outwidth = png.IHDR.width;
				first = false;
			} else if (png.IHDR.width != outwidth) {
				fprintf(stderr, "error: failed to concatenate images, inconsistent image width\n");
				ret = -1;
				break;
			}
			int j;
			for (j = 0; j < png.idat_length; j++) {
				U8* data = malloc(idat.length + (1 + 4 * png.IHDR.width) * png.IHDR.height);
				if (idat.p_data != NULL) {
					memcpy(data, idat.p_data, idat.length);
					free(idat.p_data);
				}
				idat.p_data = data;
				U64 decompressed_length;
				if (mem_inf(idat.p_data + idat.length, &decompressed_length, png.p_IDAT[j].p_data, png.p_IDAT[j].length) == 0) {
					idat.length += decompressed_length;
				} else {
					fprintf(stderr, "error: failed to decompress PNG file: %s\n", argv[i]);
					ret = -1;
					break;
				}
			}
			if (ret != 0) {
				break;
			}
			outheight += png.IHDR.height;
		} else {
			fprintf(stderr, "error: failed to load PNG file: %s\n", argv[i]);
			ret = -1;
			break;
		}
		free_png_data(&png);
	}
	if (ret == 0) {
		struct data_IHDR ihdr;
		ihdr.width = htonl(outwidth);
		ihdr.height = htonl(outheight);
		ihdr.bit_depth = 8;
		ihdr.color_type = 6;
		ihdr.compression = 0;
		ihdr.filter = 0;
		ihdr.interlace = 0;
		struct chunk chk;
		chk.length = DATA_IHDR_SIZE;
		memcpy(chk.type, "IHDR", 4);
		chk.p_data = (U8*) &ihdr;
		if (write_png_chunk(outfile, &chk) == 0) {
			U8* data = malloc(idat.length);
			U64 compressed_length = 0;
			if (mem_def(data, &compressed_length, idat.p_data, idat.length, Z_DEFAULT_COMPRESSION) == 0) {
				idat.length = compressed_length;
				free(idat.p_data);
				idat.p_data = data;
				if (write_png_chunk(outfile, &idat) == 0) {
					chk.p_data = NULL;
					chk.length = 0;
					memcpy(chk.type, "IEND", 4);
					if (write_png_chunk(outfile, &chk) == 0) {
						printf("catpng: concatenated image successfully wrote to: all.png\n");
					} else {
						fprintf(stderr, "error: failed to write IEND chunk to all.png\n");
						ret = -1;
					}
				} else {
					fprintf(stderr, "error: failed to write IDAT chunk to all.png\n");
					ret = -1;
				}
			} else {
				fprintf(stderr, "error: failed to compress PNG file: all.png\n");
				free(data);
				ret = -1;
			}
		} else {
			fprintf(stderr, "error: failed to write IHDR chunk to all.png\n");
			ret = -1;
		}
	}
	free(idat.p_data);
	fclose(outfile);
	return ret;
}
