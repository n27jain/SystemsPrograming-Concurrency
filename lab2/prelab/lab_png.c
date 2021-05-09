#include "lab_png.h"
#include "lab_file.h"


const int CHUNK_SIZE = CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + CHUNK_CRC_SIZE;

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
	if (get_chunk_from_file(chk, fp, offset, whence) != 0) {
		return -1;
	}
	*out = (struct data_IHDR*) chk->p_data;
	return 0;
}

int get_chunk_from_file(struct chunk *out, FILE *fp, long offset, int whence) {
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

int get_chunk_from_memory(U8* mem, int length, struct chunk* out) {
  out->p_data = NULL;
  U8* ptr = mem;
  int remaining = length;
  if (remaining < CHUNK_LEN_SIZE) {
    return -1;
  }
  memcpy(&out->length, ptr, CHUNK_LEN_SIZE);
  out->length = ntohl(out->length);
  remaining -= CHUNK_LEN_SIZE;
  ptr += CHUNK_LEN_SIZE;
  if (remaining < CHUNK_TYPE_SIZE) {
    return -1;
  }
  memcpy(out->type, ptr, CHUNK_TYPE_SIZE);
  remaining -= CHUNK_TYPE_SIZE;
  ptr += CHUNK_TYPE_SIZE;
	if (out->length > 0) {
    if (remaining < out->length) {
      return -1;
    }
		out->p_data = malloc(out->length);
    memcpy(out->p_data, ptr, out->length);
		remaining -= out->length;
    ptr += out->length;
	}
  if (remaining < CHUNK_CRC_SIZE) {
    return -1;
  }
  memcpy(&out->crc, ptr, CHUNK_CRC_SIZE);
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
	int result = load_png_from_file(file, &png);
	free_png_data(&png);
	return result;
}

int load_png_from_file(const char* file, struct PNG* png) {
  png->p_IDAT = NULL;
  png->idat_length = 0;
	U8* data;
  int length;
  if (load_file_raw(file, &data, &length) == 0) {
    if (load_png_from_memory(data, length, png) == 0) {
      free(data);
      return 0;
    } else {
      free(data);
      return -1;
    }
  } else {
    return -1;
  }
}

int load_png_from_memory(U8* mem, int length, struct PNG* png) {
  png->p_IDAT = NULL;
	png->idat_length = 0;
  U8* ptr = mem;
  int remaining = length;
	int ret = 0;
	if (remaining < 8 || !is_png(ptr, 8)) {
		ret = -2;
	} else {
    ptr += 8;
    remaining -= 8;
		struct chunk* chk = malloc(sizeof(struct chunk));
		if (get_chunk_from_memory(ptr, remaining, chk) == 0) {
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
            ptr += chk->length + CHUNK_SIZE;
            remaining = remaining - chk->length - CHUNK_SIZE;
						if (get_chunk_from_memory(ptr, remaining, chk) != 0) {
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
							struct chunk* idats = malloc((png->idat_length + 1) * sizeof(struct chunk));
							if (png->p_IDAT != NULL) {
								memcpy(idats, png->p_IDAT, png->idat_length * sizeof(struct chunk));
							}
							memcpy(idats + png->idat_length * sizeof(struct chunk), chk, sizeof(struct chunk));
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

int write_png_file(const char* filename, struct PNG* png) {
  FILE* fp = fopen(filename, "w");
  if (fp == NULL) {
    fclose(fp);
    return -1;
  }
  if (write_png_header(fp) != 0) {
    fclose(fp);
    return -1;
  }
  png->IHDR.width = htonl(png->IHDR.width);
  png->IHDR.height = htonl(png->IHDR.height);
  struct chunk chk;
  chk.length = DATA_IHDR_SIZE;
  memcpy(chk.type, "IHDR", 4);
  chk.p_data = (U8*)(&png->IHDR);
  if (write_png_chunk(fp, &chk) != 0) {
    fclose(fp);
    return -1;
  }
  int i;
  for (i = 0; i < png->idat_length; i++) {
    if (write_png_chunk(fp, &png->p_IDAT[i]) != 0) {
      fclose(fp);
      return -1;
    }
  }
  chk.length = 0;
  memcpy(chk.type, "IEND", 4);
  chk.p_data = NULL;
  if (write_png_chunk(fp, &chk) != 0) {
    fclose(fp);
    return -1;
  }
  fclose(fp);
  return 0;
}
