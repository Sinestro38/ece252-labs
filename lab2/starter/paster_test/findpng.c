/* findpng.c
findpng CLT - search for PNG files in a directory hierarchy

@Synopsis
findpng DIRECTORY

@description
Search for PNG files under the directory tree rooted at 
DIRECTORY and return the search results to the standard
output. The command DOES NOT follow symbolic links.

@output_format
The output of search results is a list of PNG file relative 
path names, one file pathname per line. The order of listing
the search results is not specified. If the search result is 
empty, then output "findpng: No PNG file found".

Examples:
`findpng .`
    Find PNG of the current working directory. A non-empty search results might
    look like the following:
        lab1/sandbox/new_bak.png
        lab1/sandbox/t1.png
        png_img/rgba_scanline.png
        png_img/v1.png
    It might also look like the following:
        ./lab1/sandbox/new_bak.png
        ./lab1/sandbox/t1.png
        ./png_img/rgba_scanline.png
        ./png_img/v1.png
    An empty search result will look like the following:
        findpng: No PNG file found
*/

#include <sys/types.h>  // for opendir(), readdir(), lstat()
#include <dirent.h>     // for opendir(), readdir()
#include <sys/stat.h>   // for lstat()
#include <unistd.h>     // for lstat()
#include <stdio.h>      // for printf(), fprintf(), perror()
#include <stdlib.h>     // for exit()
#include <string.h>     // for strcmp(), strcat()
#include "lab_png.h"    // for is_png(), is_png_file_valid()

bool found_png = false;

void find_png_files(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    char path[1024];

    if ((dir = opendir(dir_path)) == NULL) {
        perror("opendir");
        found_png = true;
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (lstat(path, &statbuf) == -1) {
            perror("lstat");
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            find_png_files(path);
        } else if (S_ISREG(statbuf.st_mode)) {
            if (is_png((const U8 *)path)) {
                FILE *file = fopen(path, "rb");
                if (file) {
                    if (is_png_file_valid(file)) {
                        printf("%s\n", path);
                        found_png = true;
                    }
                    fclose(file);
                }
            }
        }
    }

    closedir(dir);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    find_png_files(argv[1]);

    if (!found_png) {
        printf("findpng: No PNG file found\n");
    }

    return 0;
}



