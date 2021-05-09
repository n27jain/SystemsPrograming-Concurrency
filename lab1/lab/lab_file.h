#pragma once

#include <dirent.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>
#include <unistd.h>

#include "lab_png.h"

char** find_png_files(const char* directory, int* length);
char** combine(char** des, char** src, int l1, int l2);
bool check_extension(const char* filename, const char* extension);
void free_string_array(char** arr, int length);
bool check_catpng_filename(const char* filename, int* result);

