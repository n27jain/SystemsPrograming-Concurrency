#include "lab_file.h"

int main(const int argc, const char * const * argv) {
	if (argc != 2) {
		fprintf(stderr, "error: invalid arguments, expected <filename>\n");
		return -1;
	}
	int length = 0;
	char** files = find_png_files(argv[1], &length);
	int i;
	if (length > 0) {
		for (i = 0; i < length; i++) {
			printf("%s\n", files[i]);
		}
	} else {
		printf("findpng: No PNG file found\n");
	}
	free_string_array(files, length);
	return 0;
}
