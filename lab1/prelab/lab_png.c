#include "lab_png.h"


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
	U32 length = get_chunk_length(out);
	if (fread(out->type, CHUNK_TYPE_SIZE, 1, fp) != 1) {
		return -1;
	}
	if (length > 0) {
		out->p_data = malloc(length);
		if (fread(out->p_data, length * sizeof(U8), 1, fp) != 1) {
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

U32 get_chunk_length(struct chunk *chk) {
	return ntohl(chk->length); 
}

void free_chunk(struct chunk *chk) {
	free(chk->p_data);
	free(chk);
}

int calculate_chunk_crc(struct chunk *chk, unsigned long * result) {
	if (chk->p_data == NULL) {
		return -1;
	}
	U8* raw = malloc(get_chunk_length(chk) * sizeof(U8) + CHUNK_TYPE_SIZE);
	memcpy(raw, chk->type, CHUNK_TYPE_SIZE);
	memcpy(raw + CHUNK_TYPE_SIZE, chk->p_data, get_chunk_length(chk) * sizeof(U8));
	*result = crc(raw, get_chunk_length(chk) * sizeof(U8) + CHUNK_TYPE_SIZE);
	free(raw);
	return 0;
}


