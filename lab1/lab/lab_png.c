#include "lab_png.h"


const int CHUNK_SIZE = CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + CHUNK_CRC_SIZE + sizeof(U8*);

int is_png(U8 *buf, size_t n) {
	return n >= 8 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47;
}

int get_png_height(struct data_IHDR *buf) {
	return ntohl(buf->height);
}

int get_png_width(struct data_IHDR *buf) {
	return ntohl(buf->width);
}

int get_png_data_IHDR(struct chunk *chk, struct data_IHDR **out, FILE *fp, long offset, int whence) {
	if (get_chunk(chk, fp, offset, whence) != 0) {
		return -1;
	}
	*out = (struct data_IHDR*) chk->p_data;
	return 0;
}

int get_chunk(struct chunk *out, FILE *fp, long offset, int whence) {
	out->p_data = NULL;
	if (fseek(fp, offset, whence) != 0) {
		return -1;
	}
	if (fread(&out->length, CHUNK_LEN_SIZE, 1, fp) != 1) {
		return -1;
	}
	out->length = ntohl(out->length);
	if (fread(out->type, CHUNK_TYPE_SIZE, 1, fp) != 1) {
		return -1;
	}
	if (out->length > 0) {
		out->p_data = malloc(out->length);
		if (fread(out->p_data, out->length * sizeof(U8), 1, fp) != 1) {
			free(out->p_data);
			out->p_data = NULL;
			return -1;
		}	
	}
	if (fread(&out->crc, CHUNK_CRC_SIZE, 1, fp) != 1) {
		free(out->p_data);
		out->p_data = NULL;
		return -1;
	}
	out->crc = ntohl(out->crc);
	return 0;
}

void free_chunk(struct chunk *chk) {
	free(chk->p_data);
	free(chk);
}

int calculate_chunk_crc(struct chunk *chk, unsigned long * result) {
	if (chk->length > 0) {
		if (chk->p_data == NULL) {
			return -1;
		}
		U8* raw = malloc(chk->length * sizeof(U8) + CHUNK_TYPE_SIZE);
		memcpy(raw, chk->type, CHUNK_TYPE_SIZE);
		memcpy(raw + CHUNK_TYPE_SIZE, chk->p_data, chk->length * sizeof(U8));
		*result = crc(raw, chk->length * sizeof(U8) + CHUNK_TYPE_SIZE);
		free(raw);
	} else {
		*result = crc(chk->type, 4);
	}
	return 0;
}

int check_png_validity(const char * file) {
	struct PNG png;
	int result = load_png_file(file, &png);
	free_png_data(&png);
	return result;
}

int load_png_file(const char* file, struct PNG* png) {
	png->p_IDAT = NULL;
	png->idat_length = 0;
	FILE* fp = fopen(file, "r");
	if (fp == NULL) {
		return -1;
	}
	U8* header = malloc(8 * sizeof(U8));
	int elements_read = fread(header, sizeof(U8), 8, fp);
	int ret = 0;
	if (elements_read != 8 * sizeof(U8) || !is_png(header, 8)) {
		ret = -2;
	} else {
		struct chunk* chk = malloc(CHUNK_SIZE);
		int result = get_chunk(chk, fp, 0, SEEK_CUR);

		if (result == 0) {
			if (memcmp(chk->type, "IHDR", CHUNK_TYPE_SIZE) == 0 && chk->length == DATA_IHDR_SIZE) {
				unsigned long crc_computed = 0;
				if (calculate_chunk_crc(chk, &crc_computed) != 0) {
					ret = -3;
				}
				if (crc_computed == chk->crc) {
					memcpy(&png->IHDR, chk->p_data, DATA_IHDR_SIZE);
					png->IHDR.width = ntohl(png->IHDR.width);
					png->IHDR.height = ntohl(png->IHDR.height);
					free(chk->p_data);
					bool found_iend = false;
					while (true) {
						if (get_chunk(chk, fp, 0, SEEK_CUR) != 0) {
							ret = -4;
							break;
						}
						if (memcmp(chk->type, "IEND", CHUNK_TYPE_SIZE) == 0) {
							found_iend = true;
							if (calculate_chunk_crc(chk, &crc_computed) == 0) {
								if (crc_computed != chk->crc) {
									ret = -5;
								}
							} else {
								ret = -6;
							}
							break;
						} else if (memcmp(chk->type, "IDAT", CHUNK_TYPE_SIZE) != 0) {
							ret = -7;
							break;
						} else {
							if (calculate_chunk_crc(chk, &crc_computed) != 0) {
								ret = -8;
								break;
							}
							if (crc_computed != chk->crc) {
								ret = -9;
								break;
							}
							struct chunk* idats = malloc((png->idat_length + 1) * CHUNK_SIZE);
							if (png->p_IDAT != NULL) {
								memcpy(idats, png->p_IDAT, png->idat_length * CHUNK_SIZE);
							}
							memcpy(idats + png->idat_length * CHUNK_SIZE, chk, CHUNK_SIZE);
							free(png->p_IDAT);
							png->p_IDAT = idats;
							png->idat_length++;
						}
					}
					if (!found_iend) {
						ret = -10;
					}
				} else {
					ret = -11;
				}
			} else {
				ret = -12;
			}
		} else {
			ret = -13;
		}
		free_chunk(chk);
	}
	free(header);
	fclose(fp);
	return ret;
}

void free_png_data(struct PNG* png) {
	int i;
	for (i = 0; i < png->idat_length; i++) {
		free_chunk(&png->p_IDAT[i]);
	}
}

int write_png_header(FILE* fp) {
	return fwrite("\211PNG\r\n\032\n", 8, 1, fp) == 1 ? 0 : -1;
}

int write_png_chunk(FILE* fp, struct chunk* chk) {
	int nlength = htonl(chk->length);
	if (fwrite(&nlength, CHUNK_LEN_SIZE, 1, fp) != 1) {
		return -1;
	}
	if (fwrite(&chk->type, CHUNK_TYPE_SIZE, 1, fp) != 1) {
		return -1;
	}
	if (chk->length > 0) {
		if (fwrite(chk->p_data, sizeof(U8), chk->length, fp) != chk->length) {
			return -1;
		}
	}
	unsigned long crc;
	if (calculate_chunk_crc(chk, &crc) != 0) {
		return -1;
	}
	U32 crc_host = htonl(crc);
	if (fwrite(&crc_host, sizeof(CHUNK_CRC_SIZE), 1, fp) != 1) {
		return -1;
	}
	return 0;
}
