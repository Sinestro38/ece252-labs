/* catpng.c
catpng CLT - concatenate PNG files into a single PNG file named all.png

@Synopsis
catpng - concatenate PNG images vertically to a new PNG named all.png

@Usage
catpng PNG_FILE1 PNG_FILE2 ... PNG_FILEN

@Description
Concatenate PNG FILE(s) vertically to all.png, a new PNG file.
The concatenated image is output to a new PNG file with the name of all.png

Examples:
`catpng png_img/v1.png png_img/v2.png`
    Concatenate v1.png and v2.png vertically to all.png
*/
#include <sys/types.h>  // for opendir(), readdir(), lstat()
#include <dirent.h>     // for opendir(), readdir()
#include <sys/stat.h>   // for lstat()
#include <unistd.h>     // for lstat()
#include <stdio.h>      // for printf(), fprintf(), perror()
#include <stdlib.h>     // for exit()
#include <string.h>     // for strcmp(), strcat()
#include "crc.h"
#include "zutil.h"
#include "lab_png.h"    // for is_png(), is_png_file_valid()
#include <assert.h>

/**
 * @brief Updates the CRC field of a given PNG chunk.
 * 
 * This function calculates the CRC for the chunk data combined with the chunk type,
 * and then updates the chunk's CRC field with the new calculated CRC value.
 *
 * @param chunk A pointer to the chunk whose CRC needs to be updated.
 */
void update_chunk_crc(chunk_p chunk) {
    U8 crc_buf[CHUNK_TYPE_SIZE + chunk->length];
    // make an array that has the combined [type + data ] values in the chunk
    memcpy(crc_buf, chunk->type, CHUNK_TYPE_SIZE);
    memcpy(crc_buf + CHUNK_TYPE_SIZE, chunk->p_data, chunk->length);
    // the crc_buf now has the combined [type + data] values in the chunk
    // TODO: consider using update_crc instead for performance
    U32 new_crc = crc(crc_buf, CHUNK_TYPE_SIZE + chunk->length);
    chunk->crc = new_crc;
}

/**
 * @brief Extracts all of the given data within chunks and writes it to the png_file
 */
void write_chunks_to_png_file(FILE *png_file, struct chunk *p_IHDR, struct chunk *p_IDAT, struct chunk *p_IEND) {
    // Write the PNG signature
    U8 png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    fwrite(png_sig, 1, PNG_SIG_SIZE, png_file);

    // Write the IHDR chunk
    U32 length_be = htonl(p_IHDR->length);  // Ensure big-endian format
    U32 crc_be = htonl(p_IHDR->crc);  // Ensure big-endian format
    fwrite(&length_be, 1, CHUNK_LEN_SIZE, png_file);
    fwrite(p_IHDR->type, 1, CHUNK_TYPE_SIZE, png_file);
    fwrite(p_IHDR->p_data, 1, DATA_IHDR_SIZE, png_file); // LOOK out for ENDIAN-ness problems here
    fwrite(&crc_be, 4, 1, png_file);

    // Write the IDAT chunk
    length_be = htonl(p_IDAT->length);  // Ensure big-endian format
    crc_be = htonl(p_IDAT->crc);  // Ensure big-endian format
    fwrite(&length_be, 1, CHUNK_LEN_SIZE, png_file);
    fwrite(p_IDAT->type, 1, CHUNK_TYPE_SIZE, png_file);
    fwrite(p_IDAT->p_data, 1, p_IDAT->length, png_file);
    fwrite(&crc_be, 4, 1, png_file);
    // Write the IEND chunk
    length_be = htonl(p_IEND->length);  // Ensure big-endian format
    crc_be = htonl(p_IEND->crc);  // Ensure big-endian format
    fwrite(&length_be, 1, CHUNK_LEN_SIZE, png_file);
    fwrite(p_IEND->type, 1, CHUNK_TYPE_SIZE, png_file);
    fwrite(&crc_be, 4, 1, png_file);
}

/**
 * @brief Concatenates multiple PNG files into a single PNG file.
 *
 * This function takes an array of file paths to PNG files, reads each file, and concatenates
 * them vertically to produce a single PNG file named "all.png". The function handles the
 * reading of IHDR, IDAT, and IEND chunks, adjusts the IHDR for the combined image dimensions,
 * concatenates the IDAT data after decompression and recompression, and writes out the new
 * PNG file with updated chunks.
 *
 * @param png_files An array of strings, each representing a file path to a PNG file.
 * @param num_png_files The number of PNG files in the array.

Steps:
    1. Make a buffer to store all of the IDAT chunks
    2. For each PNG file, read the IDAT chunk, decompress it and append to buffer
    3. Update the IDAT chunk with new length and crc
    4. Write the buffer to the all.png file IDAT chunk
    5. Write the new data_IHDR->height to all_png and compute the new crc
*/
void concatenate_pngs(char **png_files, int num_png_files) {
    // Concatenate the PNG files vertically to all.png  
    FILE *all_png = fopen("all.png", "wb");
    simple_PNG_p all_png_p = (simple_PNG_p)malloc(sizeof(struct simple_PNG));
    all_png_p->p_IHDR = (struct chunk *)malloc(sizeof(struct chunk));
    all_png_p->p_IDAT = (struct chunk *)malloc(sizeof(struct chunk));
    all_png_p->p_IEND = (struct chunk *)malloc(sizeof(struct chunk));

    struct chunk *all_png_IHDR = all_png_p->p_IHDR;
    struct chunk *all_png_IDAT = all_png_p->p_IDAT;
    struct chunk *all_png_IEND = all_png_p->p_IEND;
    struct data_IHDR all_png_IHDR_data_buf;
    
    // Fill in boilerplate IHDR chunk values
    all_png_IHDR->length = DATA_IHDR_SIZE;
    memcpy(all_png_IHDR->type, "IHDR", CHUNK_TYPE_SIZE);
    all_png_IHDR->p_data = (U8*)malloc(DATA_IHDR_SIZE); // for now since it's uninitialized
    all_png_IHDR->crc = 0;

    // Initialize the IHDR data
    all_png_IHDR_data_buf.width = 0;  // will be updated later
    all_png_IHDR_data_buf.height = 0; // will be incremented later
    all_png_IHDR_data_buf.bit_depth = 8;
    all_png_IHDR_data_buf.color_type = 6;
    all_png_IHDR_data_buf.compression = 0;
    all_png_IHDR_data_buf.filter = 0;
    all_png_IHDR_data_buf.interlace = 0;

    // Fill in boilerplate IDAT 
    all_png_IDAT->length = 0; // will be incremented later
    memcpy(all_png_IDAT->type, "IDAT", CHUNK_TYPE_SIZE);
    all_png_IDAT->p_data = NULL; // will be updated later
    all_png_IDAT->crc = 0; // will be updated later

    // Fill in boilerplate IEND
    all_png_IEND->length = 0;
    memcpy(all_png_IEND->type, "IEND", CHUNK_TYPE_SIZE);
    all_png_IEND->p_data = NULL; // is zero bytes
    update_chunk_crc(all_png_IEND);

    if (all_png == NULL) {
        perror("fopen");
        exit(1);
    }

    int i;
    for (i = 0; i < num_png_files; i++) {
        FILE* png_file = fopen(png_files[i], "rb");
        // check is_png
        if (!is_png(png_files[i])) {
            fprintf(stderr, "Error: %s is not a valid PNG file\n", png_files[i]);
            exit(1);
        }
        // fetch the chunks from the file
        struct data_IHDR png_IHDR_data;
        struct chunk png_IDAT;

        get_png_data_IHDR(&png_IHDR_data, png_file);
        get_idat_chunk(&png_IDAT, png_file);

        // calculate the size of the buffers for the combined inflated png data
        const int PNG_BUF_SIZE = png_IHDR_data.height*(png_IHDR_data.width * 4 + 1);
        const int ALL_PNG_BUF_SIZE = all_png_IHDR_data_buf.height*(all_png_IHDR_data_buf.width * 4 + 1);
        U8 combined_png_buf_def[ALL_PNG_BUF_SIZE+PNG_BUF_SIZE]; /* output buffer for mem_def() */
        U8 combined_png_buf_inf[ALL_PNG_BUF_SIZE+PNG_BUF_SIZE]; /* output buffer for mem_inf() */
        U64 comb_len_def = 0;              /* compressed data length   */
        U64 comb_len_inf = 0;              /* uncompressed data length */
        int ret;

        // update the all_png_IHDR height and ensure width is the same
        if (i == 0)
            all_png_IHDR_data_buf.width = png_IHDR_data.width; // should always be the same
        assert(all_png_IHDR_data_buf.width == png_IHDR_data.width);
        
        all_png_IHDR_data_buf.height += png_IHDR_data.height;

        // inflate (decompress) the png_IDAT data and all_png IDAT data into buffers (zlib) 
        U64 second_len_inf = 0;
        if (i > 0) {
            ret = mem_inf(combined_png_buf_inf, &comb_len_inf, all_png_IDAT->p_data, all_png_IDAT->length);
            assert(ret == Z_OK);
        }
        ret = mem_inf(combined_png_buf_inf+comb_len_inf, &second_len_inf, png_IDAT.p_data, png_IDAT.length);
        assert(ret == Z_OK);
        comb_len_inf += second_len_inf;

        assert((ALL_PNG_BUF_SIZE+PNG_BUF_SIZE) == comb_len_inf);

        // compress the new all_png_IDAT data (zlib)
        ret = mem_def(combined_png_buf_def, &comb_len_def, combined_png_buf_inf, comb_len_inf, Z_DEFAULT_COMPRESSION);

        // realloc the all_png_IDAT->p_data to the new combined buffer
        all_png_IDAT->p_data = (U8*)realloc(all_png_IDAT->p_data, comb_len_def);
        memcpy(all_png_IDAT->p_data, combined_png_buf_def, comb_len_def);

        // set the length of the all_png_IDAT chunk to be the return value of mem_def
        all_png_IDAT->length = comb_len_def;

        free(png_IDAT.p_data);
    }

    // Update the IHDR chunk with the new data ptr and CRC
    U32 width_be = htonl(all_png_IHDR_data_buf.width); // ensure big-endian format for png
    U32 height_be = htonl(all_png_IHDR_data_buf.height); // ensure big-endian format for png
    memcpy(all_png_IHDR->p_data, &width_be, 4);
    memcpy(all_png_IHDR->p_data + 4, &height_be, 4);
    memcpy(all_png_IHDR->p_data + 8, &all_png_IHDR_data_buf.bit_depth, 1);
    memcpy(all_png_IHDR->p_data + 9, &all_png_IHDR_data_buf.color_type, 1);
    memcpy(all_png_IHDR->p_data + 10, &all_png_IHDR_data_buf.compression, 1);
    memcpy(all_png_IHDR->p_data + 11, &all_png_IHDR_data_buf.filter, 1);
    memcpy(all_png_IHDR->p_data + 12, &all_png_IHDR_data_buf.interlace, 1);
    update_chunk_crc(all_png_IHDR);

    update_chunk_crc(all_png_IDAT);

    // Writing to all.png directly
    write_chunks_to_png_file(all_png, all_png_IHDR, all_png_IDAT, all_png_IEND);

    // Close the all.png file
    fclose(all_png);

    // Cleanup memory
    free(all_png_IHDR->p_data);
    free(all_png_IHDR);
    free(all_png_IDAT->p_data);
    free(all_png_IDAT);
    free(all_png_IEND);
    free(all_png_p);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s PNG_FILE1 [PNG_FILE2 ... PNG_FILEN]\n", argv[0]);
        exit(1);
    }
    
    else if (argc == 2) {
        // There's only one PNG file so copy the contents of the first PNG file to all.png
        FILE *png_file = fopen(argv[1], "rb");
        if (png_file == NULL) {
            perror("fopen");
            exit(1);
        }
        // Copy the file to a new file like "cp first_img.png all.png"
        FILE *all_png = fopen("all.png", "wb");
        if (all_png == NULL) {
            perror("fopen");
            exit(1);
        }
        char buffer[BUFSIZ];
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), png_file)) > 0) {
            if (fwrite(buffer, 1, n, all_png) != n) {
                perror("fwrite");
                fclose(png_file);
                fclose(all_png);
                exit(1);
            }
        }
        fclose(png_file);
        fclose(all_png);
    }

    else {
        // There are more than one PNG file so concatenate them vertically to all.png
        const int num_png_files = argc - 1;
        char **png_files = argv + 1;
        concatenate_pngs(png_files, num_png_files);
    }

    return 0;
}

