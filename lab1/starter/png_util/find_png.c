/* find_png.c
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

/**
 * @brief This function recursively searches for PNG files in a directory.
 * 
 * This function takes a directory path as input and recursively searches for PNG files.
 * If a PNG file is found, its relative path is printed to the standard output.
 * If no PNG file is found, it prints "findpng: No PNG file found".
 * 
 * @param dir_path The directory path to start the search from.
 */
void find_png_files(const char *dir_path) {
    DIR *dir;  // Directory pointer
    struct dirent *entry;  // Directory entry
    struct stat statbuf;  // File status
    char path[1024];  // File path
    bool found_png = false;  // Flag to indicate if a PNG file is found

    // Open the directory
    if ((dir = opendir(dir_path)) == NULL) {
        perror("opendir");
        return;
    }

    // Read each entry in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Construct the full path of the entry
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        // Get the status of the entry
        if (lstat(path, &statbuf) == -1) {
            perror("lstat");
            continue;
        }

        // If the entry is a directory, recursively search it
        if (S_ISDIR(statbuf.st_mode)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue; // Skip the current and parent directory entries
            }
            find_png_files(path);
        } 
        // If the entry is a regular file, check if it's a PNG file
        else if (S_ISREG(statbuf.st_mode)) {
            if (is_png((const U8 *)path)) {             // Check if the file is a PNG
                FILE *file = fopen(path, "rb");         // Open the file in binary read mode
                if (file) {
                    // If the PNG file is valid, print its path
                    if (is_png_file_valid(file)) {
                        // Print if the PNG file is invalid
                        printf("%s\n", path);
                        found_png = true;
                    }
                    fclose(file);
                }
            }
        }
    }

    // Close the directory
    closedir(dir);

    // If no PNG file is found, print a message
    if (!found_png) {
        printf("findpng: No PNG file found\n");
    }
}
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <directory>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    find_png_files(argv[1]);

    return 0;
}
