
#include "lab_png.h"

const int CHUNK_SIZE = CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + CHUNK_CRC_SIZE + sizeof(U8*);

int main(const int argc, const char * const * argv) {
	if (argc != 2) {
		fprintf(stderr, "error: invalid arguments, expected <filename>\n");
		return -1;
	}
	FILE* fp = fopen(argv[1], "r");
	if (fp == NULL) {
		fprintf(stderr, "error: failed to open file: %d\n", errno);
		return -1;
	}
	U8* header = malloc(8 * sizeof(U8));
	int elements_read = fread(header, sizeof(U8), 8, fp);
	int ret = 0;
	if (elements_read != 8 * sizeof(U8) || !is_png(header, 8)) {
		printf("%s: Not a png file\n", argv[1]);
		ret = -1;
	} else {
		printf("%s: ", argv[1]);
		struct chunk* chk = malloc(CHUNK_SIZE);
		struct data_IHDR* ihdr;
		int result = get_png_data_IHDR(chk, &ihdr, fp, 0, SEEK_CUR);
		if (result == 0) {
			if (memcmp(chk->type, "IHDR", CHUNK_TYPE_SIZE) == 0) {
			printf("%d x %d\n", get_png_width(ihdr), get_png_height(ihdr));
			unsigned long crc_computed = 0;
				if (calculate_chunk_crc(chk, &crc_computed) != 0) {
					fprintf(stderr, "error: failed to calculate IHDR CRC\n");
					ret = -1;
				}
				if (crc_computed == chk->crc) {
					while (true) {
						free(chk->p_data);
						if (get_chunk(chk, fp, 0, SEEK_CUR) != 0) {
							fprintf(stderr, "error: failed to read IDAT chunk\n");
							ret = -1;
							break;
						}
						if (memcmp(chk->type, "IEND", CHUNK_TYPE_SIZE) == 0) {
							break;
						} else if (memcmp(chk->type, "IDAT", CHUNK_TYPE_SIZE) != 0) {
							fprintf(stderr, "error: invalid chunk type: %c%c%c%c\n", chk->type[0], chk->type[1], chk->type[2], chk->type[3]);
							ret = -1;
							break;
						} else {
							if (calculate_chunk_crc(chk, &crc_computed) != 0) {
								fprintf(stderr, "error: failed to calculate IDAT CRC\n");
								ret = -1;
								break;
							}
							if (crc_computed != chk->crc) {
								fprintf(stderr, "IDAT chunk CRC error: computed %08lx, expected %08x\n", crc_computed, chk->crc);	
								ret = -1;
								break;
							}
						}
					}
				} else {
					fprintf(stderr, "IHDR chunk CRC error: computed %08lx, expected %08x\n", crc_computed, chk->crc);	
					ret = -1;
				}
			} else {
				fprintf(stderr, "error: IHDR chunk not found\n");
				ret = -1;
			}
		} else {
			fprintf(stderr, "error: invalid IHDR chunk\n");
			ret = -1;
		}
		free_chunk(chk);
	}
	free(header);
	fclose(fp);
	return ret;
}
