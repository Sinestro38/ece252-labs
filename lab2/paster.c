#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <getopt.h>
#include <stdatomic.h>
#include <semaphore.h>
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"
#include "assert.h"

#define ECE252_HEADER "X-Ece252-Fragment: "
#define URL_LENGTH 256
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

atomic_int fragment_counter = 0; // Counts the number of fragments completed
volatile int fragment_numbers[50]; // Stores the fragment numbers

/*
 * Use semaphore to handle the synchronization issue.
 */
sem_t sem;

typedef struct recv_buf2 {
    char *buf;       /* Memory to hold a copy of received data */
    size_t size;     /* Size of valid data in buf in bytes */
    size_t max_size; /* Max capacity of buf in bytes */
    int seq;         /* >=0 sequence number extracted from HTTP header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);

/**
 * @brief cURL header callback function to extract image sequence number from
 *        HTTP header data. An example header for image part n (assume n = 2) is:
 *        X-Ece252-Fragment: 2
 * @param p_recv Header data delivered by cURL
 * @param size Size of each member
 * @param nmemb Number of members
 * @param userdata User-defined data structure
 * @return Size of header data received.
 * @details This routine will be invoked multiple times by libcurl until the full
 *          header data are received. We are only interested in the ECE252_HEADER line
 *          received so that we can extract the image sequence number from it.
 */
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata) {
    int realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF*)userdata;

    if (realsize > (int)strlen(ECE252_HEADER) &&
        strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {
        /* Extract image sequence number */
        p->seq = atoi(p_recv + strlen(ECE252_HEADER));
    }
    return realsize;
}

/**
 * @brief Write callback function to save a copy of received data in RAM.
 *        The received libcurl data are pointed by p_recv,
 *        which is provided by libcurl and is not user-allocated memory.
 *        The user-allocated memory is at p_userdata. One needs to
 *        cast it to the proper struct to make good use of it.
 *        This function may be invoked more than once by one invocation of
 *        curl_easy_perform().
 * @param p_recv Pointer to received data
 * @param size Size of each member
 * @param nmemb Number of members
 * @param p_userdata User-defined data structure
 * @return Size of data received.
 */
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata) {
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;

    if (p->size + realsize + 1 > p->max_size) { /* Hope this rarely happens */
        /* Received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);
        char *q = (char*) realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* Out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /* Copy data from libcurl */
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

/**
 * @brief Initialize the receive buffer.
 * @param ptr Pointer to the receive buffer structure
 * @param max_size Maximum size of the buffer
 * @return 0 on success, 1 if ptr is NULL, 2 if malloc fails
 */
int recv_buf_init(RECV_BUF *ptr, size_t max_size) {
    void *p = NULL;

    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
        return 2;
    }

    ptr->buf = (char*) p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1; /* Valid seq should be non-negative */
    return 0;
}

/**
 * @brief Clean up the receive buffer.
 * @param ptr Pointer to the receive buffer structure
 * @return 0 on success, 1 if ptr is NULL
 */
int recv_buf_cleanup(RECV_BUF *ptr) {
    if (ptr == NULL) {
        return 1;
    }

    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

/**
 * @brief Output data in memory to a file.
 * @param path Output file path
 * @param in Input data to be written to the file
 * @param len Length of the input data in bytes
 * @return 0 on success, negative value on error
 */
int write_file(const char *path, const void *in, size_t len) {
    FILE *fp = NULL;

    if (path == NULL) {
        fprintf(stderr, "write_file: file name is null!\n");
        return -1;
    }

    if (in == NULL) {
        fprintf(stderr, "write_file: input data is null!\n");
        return -1;
    }

    fp = fopen(path, "wb");
    if (fp == NULL) {
        perror("fopen");
        return -2;
    }

    if (fwrite(in, 1, len, fp) != len) {
        fprintf(stderr, "write_file: incomplete write!\n");
        return -3;
    }
    return fclose(fp);
}

/**
 * @brief Function to create a thread.
 *        Uses cURL to get the fragment numbers and file data to store in files.
 *        Creates a file for a specific fragment of the image.
 * @param thread_input Input data for the thread
 * @return NULL
 */
void* get_fragment(void* thread_input) {
    int server_num = 1;
    char **server_urls = (char**) thread_input;

    while (fragment_counter < 50) {
        CURL *curl_handle;
        CURLcode res;
        RECV_BUF recv_buf;
        char fname[URL_LENGTH];

        // Initialize buffer
        if (recv_buf_init(&recv_buf, BUF_SIZE) != 0) {
            fprintf(stderr, "recv_buf_init failed\n");
            continue;
        }

        // Init a curl session
        curl_handle = curl_easy_init();
        if (curl_handle == NULL) {
            fprintf(stderr, "curl_easy_init: returned NULL\n");
            recv_buf_cleanup(&recv_buf);
            continue;
        }

        // Specify URL to get
        curl_easy_setopt(curl_handle, CURLOPT_URL, server_urls[server_num - 1]);
        // Register write callback function to process received data
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
        // User-defined data structure passed to the callback function
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
        // Register header callback function to process received header data
        curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
        // User-defined data structure passed to the callback function
        curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
        // Some servers require a user-agent field
        curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        // Get it!
        res = curl_easy_perform(curl_handle);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        // Check whether the sequence number already exists
        sem_wait(&sem);
        if (recv_buf.seq >= 0 && recv_buf.seq < 50 && fragment_numbers[recv_buf.seq] == -1) {
            fragment_numbers[recv_buf.seq] = recv_buf.seq;
            sprintf(fname, "./_tmp/%d.png", recv_buf.seq + 1);
            write_file(fname, recv_buf.buf, recv_buf.size);
            fragment_counter += 1;

            // (Optional) LOADING bar
            int progress = (fragment_counter) * 100 / 50; // Calculate progress percentage
            char loading_bar[50+1];
            memset(loading_bar, ' ', 50);
            loading_bar[50] = '\0';
            int filled_length = fragment_counter; // Calculate filled length for the loading bar
            for (int i = 0; i < filled_length && i < 50; i++) {
                loading_bar[i] = '=';
            }
            if (filled_length < 50) {
                loading_bar[filled_length] = '>';
            }
            printf("\rDownloading img strips: [%-50s] %d%%", loading_bar, progress);
            fflush(stdout);
        }
        sem_post(&sem);

        // Cleaning up
        curl_easy_cleanup(curl_handle);
        recv_buf_cleanup(&recv_buf);

        // For using different server
        server_num += 1;
        if (server_num == 4) {
            server_num = 1;
        }
    }
    return NULL;
}

/**
 * @brief Handle inputs, modify URLs based on user inputs, create threads to get data, and concatenate PNGs.
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, negative value on error
 */
int main(int argc, char **argv) {
    int c;
    int t = 1;   // Number of threads
    int n = 1;   // Image number
    char *str = "option requires an argument";
    memset(&fragment_numbers, -1, sizeof(fragment_numbers));

    // Handle inputs
    while ((c = getopt(argc, argv, "t:n:")) != -1) {
        switch (c) {
        case 't':
            t = strtoul(optarg, NULL, 10);
            if (t <= 0) {
                fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                return -1;
            }
            break;

        case 'n':
            n = strtoul(optarg, NULL, 10);
            if (n <= 0 || n > 3) {
                fprintf(stderr, "%s: %s 1, 2, or 3 -- 'n'\n", argv[0], str);
                return -1;
            }
            break;

        default:
            return -1;
        }
    }

    // Allocate memory for modified URLs
    char **modified_url = malloc(3 * sizeof(char*));
    if (modified_url == NULL) {
        fprintf(stderr, "malloc failed for modified_url\n");
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        modified_url[i] = malloc(URL_LENGTH * sizeof(char));
        if (modified_url[i] == NULL) {
            fprintf(stderr, "malloc failed for modified_url[%d]\n", i);
            return -1;
        }
    }

    // Modify URLs
    sprintf(modified_url[0], "http://ece252-1.uwaterloo.ca:2520/image?img=%d", n);
    sprintf(modified_url[1], "http://ece252-2.uwaterloo.ca:2520/image?img=%d", n);
    sprintf(modified_url[2], "http://ece252-3.uwaterloo.ca:2520/image?img=%d", n);

    // Initialize semaphore
    sem_init(&sem, 0, 1);

    // Initialize threads
    const int thread_num = t;
    pthread_t tid[thread_num];

    // Initialize libcurl before any thread
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Create threads
    int current_thread_num = t;
    for (int i = 0; i < t; i++) {
        if (pthread_create(&tid[i], NULL, get_fragment, modified_url) != 0) {
            current_thread_num = i;
            // Thread num should be from 0 to t-1
            printf("Thread #%d failed to create!\n", i);
            // This issue could be mainly a capacity issue because the number of threads we can generate is limited by the ECEubuntu server.
            // We need to stop generating new threads.
            break;
        }
    }

    // Join threads based on how many threads we generated.
    for (int i = 0; i < current_thread_num; i++) {
        pthread_join(tid[i], NULL);
    }
   printf("\nJoined all threads...\n");

    // Store 50 fragment file names
    char **fragment_files = malloc(fragment_counter * sizeof(char*));
    if (fragment_files == NULL) {
        fprintf(stderr, "malloc failed for fragment_files\n");
        return -1;
    }
    for (int i = 0; i < fragment_counter; i++) {
        fragment_files[i] = malloc(256 * sizeof(char));
        if (fragment_files[i] == NULL) {
            fprintf(stderr, "malloc failed for fragment_files[%d]\n", i);
            return -1;
        }
        sprintf(fragment_files[i], "./_tmp/%d.png", i + 1);
    }

   printf("Concatenating PNG segments into a single PNG file...\n");
    // Concatenate PNG segments into a single PNG file
    concatenate_pngs(fragment_files, fragment_counter);

   printf("Cleaning up...\n");
    // Deallocate and remove helper files
    for (int i = 0; i < fragment_counter; i++) {
        remove(fragment_files[i]);
        free(fragment_files[i]);
    }
    free(fragment_files);

    // Deallocate URLs
    for (int i = 0; i < 3; i++) {
        free(modified_url[i]);
    }
    free(modified_url);

    // Destroy semaphore and cleanup libcurl
    sem_destroy(&sem);
    curl_global_cleanup();
    printf("Done! You can now view the image by typing 'display ./all.png' in the terminal.\n");
    pthread_exit(0);

}

