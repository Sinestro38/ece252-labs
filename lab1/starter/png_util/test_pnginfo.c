#include <stdio.h>    /* for printf(), perror()...   */
#include <stdlib.h>   /* for malloc()                */
#include <errno.h>    /* for errno                   */
#include <stdbool.h>
#include "crc.h"      /* for crc()                   */
#include "zutil.h"    /* for mem_def() and mem_inf() */
#include "lab_png.h"  /* simple PNG data structures  */

int main (int argc, char **argv) { // commented out main for downstream testing
    // Create a global buffer
    U8 *p_buffer = NULL;
    p_buffer = malloc(256*8);
    bool is_file_corrupt = true;

    // Extract the filename from the command line arguments
    const char *filename = argv[1];

    // Step 1: Open the PNG file
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("fopen");
        return errno;
    }

    // Step 2: Check if the file is a valid PNG
    if (!is_png(filename)) {
        fprintf(stderr, "The file is not a valid PNG.\n");
        fclose(fp);
        return -1;
    }

    // Step 3: Read IHDR data
    struct data_IHDR ihdr_data;
    get_png_data_IHDR(&ihdr_data, fp);
    printf("Width: %u, Height: %u, Bit Depth: %u, Color Type: %u\n",
           ihdr_data.width, ihdr_data.height, ihdr_data.bit_depth, ihdr_data.color_type);


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
    get_idat_chunk(fp, idat_chk);

    // Calculate CRC for IDAT chunk
    U8 *idat_crc_buf = (U8 *)malloc(CHUNK_TYPE_SIZE + idat_chk->length);
    memcpy(idat_crc_buf, "IDAT", CHUNK_TYPE_SIZE);
    memcpy(idat_crc_buf + CHUNK_TYPE_SIZE, idat_chk->p_data, idat_chk->length);
    U32 idat_calculated_crc = crc(idat_crc_buf, CHUNK_TYPE_SIZE + idat_chk->length);

    is_file_corrupt &= idat_calculated_crc == idat_chk->crc;

    // Compare calculated CRC with the CRC read from the file
    if (idat_calculated_crc != idat_chk->crc) {
        fprintf(stderr, "IDAT CRC mismatch: calculated 0x%x, expected 0x%x\n", idat_calculated_crc, idat_chk->crc);
        free(idat_crc_buf);
        fclose(fp);
        return -1;
    } else {
        printf("IDAT CRC check passed: 0x%x\n", idat_calculated_crc);
        free(idat_crc_buf);
    }

    // Step 6: Compare the calculated CRC with the file CRC
    if (is_file_corrupt) {
        fprintf(stderr, "CRC mismatch: calculated 0x%x, expected 0x%x\n", calculated_crc, file_crc);
        fclose(fp);
        return -1;
    } else {
        printf("CRC check passed: 0x%x\n", calculated_crc);
    }

    // Step 7: Clean up
    fclose(fp);
    free(p_buffer);
    free(idat_chk);

    return 0;
}
