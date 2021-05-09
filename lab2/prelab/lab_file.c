#include "lab_file.h"


int get_file_size(FILE* fp, int* length) {
  if (fseek(fp, 0, SEEK_END) != 0) {
    return -1;
  }
  *length = ftell(fp);
  if (*length < 0) {
    return -1;  
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    return -1;
  }
  return 0;
}

int load_file_raw(const char* filename, unsigned char** data, int* length) {
  FILE* fp = fopen(filename, "r");
  if (fp == NULL) {
    return -1;
  }
  if (get_file_size(fp, length) != 0) {
    return -1;
  }
  *data = malloc(*length * sizeof(unsigned char));
  if (fread(*data, 1, *length, fp) != *length) {
    free(*data);
    return -1;
  }
  fclose(fp);
  return 0;
}

char ** find_png_files(const char * directory, int * length) {
	
	char** result = NULL;
	DIR* d;
	struct dirent* dir;
	d = opendir(directory);
	if (d == NULL) {
		return NULL;
	}
	struct stat stt;
	dir = readdir(d);
	while (dir != NULL) {
		int filepath_length = strlen(directory) + 1 + strlen((char*) dir->d_name);
		char* filepath = calloc(filepath_length, filepath_length);
		strcat(filepath, directory);
		strcat(filepath, "/");
		strcat(filepath, (char*) dir->d_name);
		if (strcmp(dir->d_name, ".") != 0 && strcmp(dir->d_name, "..") != 0 && lstat(filepath, &stt) == 0) {
			if (S_ISDIR(stt.st_mode)) {
				int l = 0;
				char** files = find_png_files(filepath, &l);
				if (result == NULL) {
					result = files;
					*length = l;
				} else if (files != NULL) {
					char** a = combine(result, files, *length, l);
					free_string_array(result, *length);
					free_string_array(files, l);
					*length += l;
					result = a;
				}
			} else if (S_ISLNK(stt.st_mode)) {
				// Do nothing
			} else if (S_ISREG(stt.st_mode)) {
				if (check_extension(filepath, ".png") && check_png_validity(filepath) == 0) {
					char** a = combine(result, &filepath, *length, 1);
					free_string_array(result, *length);
					result = a;
					*length += 1;
				}
			}
		}
		free(filepath);
		dir = readdir(d);
	}

	closedir(d);

	return result;
}

char** combine(char** des, char** src, int l1, int l2) {
	char** result = malloc((l1 + l2) * sizeof(char*));
	int i;
	for (i = 0; i < l1; i++) {
		result[i] = malloc((strlen(des[i]) + 1) * sizeof(char));
		strcpy(result[i], des[i]);
	}
	for (i = 0; i < l2; i++) {
		result[i + l1] = malloc((strlen(src[i]) + 1) * sizeof(char));
		strcpy(result[i + l1], src[i]);
	}

	return result;
}

bool check_extension(const char* filename, const char* extension) {
	return strcmp(filename + strlen(filename) - strlen(extension), extension) == 0;
}

void free_string_array(char** arr, int length) {
	int i;
	for (i = 0; i < length; i++) {
		free(arr[i]);
	}
	free(arr);
}

bool check_catpng_filename(const char* filename, int* result) {
	size_t i = strlen(filename) - 5; // exclude ".png" extension
	int power = 0;
	while (i >= 0 && isdigit(filename[i])) {
		*result += (filename[i] - '0') * pow(10, power);
		power++;
		i--;
	}
	return i >= 0 && filename[i] == '_';
}
