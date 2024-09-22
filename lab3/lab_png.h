/** lab_png.h
 * @brief  micros and structures for a simple PNG file 
 *
 * Copyright 2018-2020 Yiqing Huang
 *
 * This software  may be freely redistributed under the terms of MIT License
 */
#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdio.h>
#include <stdbool.h>

/******************************************************************************
 * DEFINED MACROS 
 *****************************************************************************/

#define PNG_SIG_SIZE    8 /* number of bytes of png image signature data */
#define CHUNK_LEN_SIZE  4 /* chunk length field size in bytes */          
#define CHUNK_TYPE_SIZE 4 /* chunk type field size in bytes */
#define CHUNK_CRC_SIZE  4 /* chunk CRC field size in bytes */
#define DATA_IHDR_SIZE 13 /* IHDR chunk data field size */

/******************************************************************************
 * STRUCTURES and TYPEDEFS 
 *****************************************************************************/
typedef unsigned char U8;
typedef unsigned int  U32;

typedef struct chunk {
    U32 length;  /* length of data in the chunk, host byte order */
    U8  type[4]; /* chunk type */
    U8  *p_data; /* pointer to location where the actual data are */
    U32 crc;     /* CRC field  */
} *chunk_p;

/* note that there are 13 Bytes valid data, compiler will padd 3 bytes to make
   the structure 16 Bytes due to alignment. So do not use the size of this
   structure as the actual data size, use 13 Bytes (i.e DATA_IHDR_SIZE macro).
 */
typedef struct data_IHDR {// IHDR chunk data 
    U32 width;        /* width in pixels, big endian   */
    U32 height;       /* height in pixels, big endian  */
    U8  bit_depth;    /* num of bits per sample or per palette index.
                         valid values are: 1, 2, 4, 8, 16 */
    U8  color_type;   /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                         =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8  compression;  /* only method 0 is defined for now */
    U8  filter;       /* only method 0 is defined for now */
    U8  interlace;    /* =0: no interlace; =1: Adam7 interlace */
} *data_IHDR_p;

/* A simple PNG file format, three chunks only*/
typedef struct simple_PNG {
    struct chunk *p_IHDR;
    struct chunk *p_IDAT;  /* only handles one IDAT chunk */  
    struct chunk *p_IEND;
} *simple_PNG_p;

/******************************************************************************
 * FUNCTION PROTOTYPES 
 *****************************************************************************/
int is_png(const U8 *buf_path_name);
int get_png_height(struct data_IHDR *buf);
int get_png_width(struct data_IHDR *buf);
void get_png_data_IHDR(struct data_IHDR *out, FILE *fp);

/**
 * @brief Reads the CRC value from the PNG file.
 *
 * @param fp File pointer to the PNG file at the CRC position.
 * @return CRC value.
 */
U32 read_crc(FILE *fp);

/**
 * @brief Gets the IDAT chunk from the PNG file.
 *
 * @param idat_chunk Pointer to the structure to store IDAT chunk data.
 * @param fp File pointer to the PNG file.
 */
void get_idat_chunk(chunk_p idat_chunk, FILE *fp);

/**
 * @brief Check CRC of the PNG file IHDR and IDAT chunks.
 * 
 * This function validates the CRC of the IHDR and IDAT chunks in a PNG file.
 * 
 * @param fp The file pointer to the PNG file.
 * @return true if the file is valid, false otherwise.
 */
bool is_png_file_valid(FILE *fp);

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
void concatenate_pngs(char **png_files, int num_png_files);
