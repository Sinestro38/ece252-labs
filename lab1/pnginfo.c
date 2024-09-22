/*
pnginfo.c
*/
#include <stdio.h>    /* for printf(), perror()...   */
#include <stdlib.h>   /* for malloc()                */
#include <errno.h>    /* for errno                   */
#include <stdbool.h>
#include "crc.h"      /* for crc()                   */
#include "zutil.h"    /* for mem_def() and mem_inf() */
#include "lab_png.h"  /* simple PNG data structures  */

/*
The goal is to create a command line program named pnginfo that
prints the dimensions of a valid PNG image file and an error message 
to the standard output if the input file is not a PNG file or is a corrupted 
PNG file. The command takes one input argument, which is the path name of a 
file. Both absolute path name and relative path name are accepted. 
For example, command ./pnginfo WEEF 1.png will output the following line:
*/

int is_png(const U8* buf_path_name)
{
    U8 png_sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    U8 file_sig[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    FILE *file = fopen(buf_path_name, "rb");

    if (file == NULL)
        printf("error");

    // Load the first 8 bytes of the file into file_sig
    fread(file_sig, 1, PNG_SIG_SIZE, file);

    // Compare the first 8 bytes of the file with the PNG signature for a match
    int num_bytes_off = memcmp(file_sig, png_sig, 8);
    bool is_png = (num_bytes_off == 0);

    fclose(file);

    return is_png;
}



int get_png_height(struct data_IHDR *buf) {
    return buf->height;
}

int get_png_width(struct data_IHDR *buf) {
    return buf->width;
}

void get_png_data_IHDR(struct data_IHDR *out, FILE *fp) {
    // Seek to the beginning of the file
    fseek(fp, 0, SEEK_SET);

    // Skip the PNG sig (8B)
    fseek(fp, PNG_SIG_SIZE, SEEK_CUR);

    // Skip the IHDR chunk length (4B) & type (4B)
    fseek(fp, CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE, SEEK_CUR);

    // Create a temporary buffer to store the data
    U8 *data = (U8 *)malloc(DATA_IHDR_SIZE);

    // Read the data from the file
    fread(data, 1, DATA_IHDR_SIZE, fp);

    // Copy the data into the output buffer
    out->width = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    out->height = (data[4] << 24) | (data[5] << 16) | (data[6] << 8) | data[7];
    out->bit_depth = data[8];
    out->color_type = data[9];
    out->compression = data[10];
    out->filter = data[11];
    out->interlace = data[12];

    free(data);
    data = NULL;
}

/**
 * @brief read_crc function reads the CRC from a PNG file.
 * 
 * This function reads the CRC from a PNG file and returns it as a U32.
 * 
 * @param fp The file pointer to the PNG file (ASSUMED to be at correct starting position).
 * @return The CRC value read from the file.
 */
U32 read_crc(FILE *fp) {
    unsigned char crc_bytes[4];
    fread(crc_bytes, 1, 4, fp);
    return (crc_bytes[0] << 24) | (crc_bytes[1] << 16) | (crc_bytes[2] << 8) | crc_bytes[3];
}

/**
 * @brief get_idat_chunk function retrieves the IDAT chunk from a PNG file.
 * 
 * This function reads the IDAT chunk from a PNG file, including the chunk length, 
 * type, and data. It also verifies that the chunk type is indeed "IDAT".
 * 
 * @param idat_chunk A pointer to a chunk structure to store the IDAT chunk data.
 * @param fp The file pointer to the PNG file (already opened but can be at any position).
 */

void get_idat_chunk(chunk_p idat_chunk, FILE *fp) {
    // Seek to the beginning of the IDAT chunk
    fseek(fp, PNG_SIG_SIZE + CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + DATA_IHDR_SIZE + CHUNK_CRC_SIZE, SEEK_SET);

    // Read the length of the IDAT chunk
    U32 length;
    fread(&length, 1, 4, fp);
    length = (length << 24) | ((length << 8) & 0x00FF0000) | ((length >> 8) & 0x0000FF00) | (length >> 24);

    idat_chunk->length = length;

    // Read the chunk type and verify it's IDAT
    U8* chunk_type = malloc(4);
    fread(chunk_type, 1, 4, fp);
    if (memcmp(chunk_type, "IDAT", 4) != 0) {
        fprintf(stderr, "Expected IDAT chunk but found something else.\n");
        fclose(fp);
        exit(EXIT_FAILURE);
    }
    memcpy(idat_chunk->type, chunk_type, 4);

    free(chunk_type);
    chunk_type = NULL;

    // Allocate memory for the data
    idat_chunk->p_data = malloc(length);

    // Read the chunk data
    fread(idat_chunk->p_data, 1, length, fp);

    // Read the CRC from the file
    idat_chunk->crc = read_crc(fp);
}

/**
 * @brief Check CRC of the PNG file IHDR and IDAT chunks.
 * 
 * This function validates the CRC of the IHDR and IDAT chunks in a PNG file.
 * 
 * @param fp The file pointer to the PNG file.
 * @return true if the file is valid, false otherwise.
 */
bool is_png_file_valid(FILE *fp) {
    bool is_file_corrupt = true;
    bool _output_logs = false;
    // Step 4: Calculate CRC for IHDR data
    fseek(fp, PNG_SIG_SIZE + CHUNK_LEN_SIZE, SEEK_SET);
    U8 ihdr_crc_buf[CHUNK_TYPE_SIZE + DATA_IHDR_SIZE];
    fread(ihdr_crc_buf, 1, DATA_IHDR_SIZE, fp);
    U32 calculated_crc = crc(ihdr_crc_buf, CHUNK_TYPE_SIZE + DATA_IHDR_SIZE);

    // Step 5: Read the existing CRC from the file
    U32 file_crc = read_crc(fp); // ASSUMES fp is at correct IHDR crc position
    is_file_corrupt &= calculated_crc == file_crc; // Check if matching

    // Step 6: Seek to the IDAT chunk and read it
    fseek(fp, PNG_SIG_SIZE 
            + CHUNK_LEN_SIZE 
            + CHUNK_TYPE_SIZE 
            + DATA_IHDR_SIZE 
            + CHUNK_CRC_SIZE, 
            SEEK_SET);

    // Read the length of the IDAT chunk
    U32 idat_len = 0;
    fread(&idat_len, 1, 4, fp);

    // Step 7: Read IDAT chunk and check CRC
    chunk_p idat_chk = (chunk_p)malloc(sizeof(struct chunk));
    get_idat_chunk(idat_chk, fp);

    // Calculate CRC for IDAT chunk
    U8 *idat_crc_buf = (U8 *)malloc(CHUNK_TYPE_SIZE + idat_chk->length);
    memcpy(idat_crc_buf, "IDAT", CHUNK_TYPE_SIZE);
    memcpy(idat_crc_buf + CHUNK_TYPE_SIZE, idat_chk->p_data, idat_chk->length);
    U32 idat_calculated_crc = crc(idat_crc_buf, CHUNK_TYPE_SIZE + idat_chk->length);

    is_file_corrupt &= idat_calculated_crc == idat_chk->crc;

    // // Compare calculated CRC with the CRC read from the file
    // if (idat_calculated_crc != idat_chk->crc) {
    //     fprintf(stderr, "IDAT CRC mismatch: calculated 0x%x, expected 0x%x\n", idat_calculated_crc, idat_chk->crc);
    // } else {
    //     printf("IDAT CRC check passed: 0x%x\n", idat_calculated_crc);
    // }

    // // Step 6: Compare the calculated CRC with the file CRC
    // if (is_file_corrupt) {
    //     fprintf(stderr, "CRC mismatch: calculated 0x%x, expected 0x%x\n", calculated_crc, file_crc);
    // } else {
    //     printf("CRC check passed: 0x%x\n", calculated_crc);
    // }

    if (is_file_corrupt) {
        fprintf(stderr, "CRC mismatch: calculated 0x%x, expected 0x%x\n", calculated_crc, file_crc);
    }
    free(idat_crc_buf);
    free(idat_chk);
    return !is_file_corrupt;
}

