#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdatomic.h>
#include <semaphore.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"
#include "shm_stack.h"

#define ECE252_HEADER "X-Ece252-Fragment: "
#define URL_LENGTH 256
#define BUF_SIZE 10240  /* 10K */
#define BUF_INC  524288   /* 1024*512  = 0.5M */
#define MAX_FRAGMENTS 50
#define NUM_SEMS 5
#define SHARED_SEM 1
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

typedef struct recv_buf_flat {
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
int perform_curl_request(const char *url, RECV_BUF *recv_buf);

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
 * @brief Helper function to perform a cURL request.
 * @param url The URL to fetch from
 * @param recv_buf Pointer to the receive buffer structure
 * @return 0 on success, negative value on error
 */
int perform_curl_request(const char *url, RECV_BUF *recv_buf) {
    CURL *curl_handle;
    CURLcode res;

    curl_handle = curl_easy_init();
    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return -1;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)recv_buf);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl);
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    /* get it! */
    res = curl_easy_perform(curl_handle);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl_handle);
        return -2;
    }

    curl_easy_cleanup(curl_handle);
    return 0;
}


/**
 * @brief Cleans up all shared resources used by the program.
 * 
 * This function detaches shared memory segments, removes them, and destroys semaphores.
 * 
 * @param num_produced Pointer to the shared counter for produced items
 * @param num_consumed Pointer to the shared counter for consumed items
 * @param sems Pointer to the array of semaphores
 * @param queue Pointer to the shared queue
 * @param shmid_num_produced Shared memory ID for num_produced
 * @param shmid_num_consumed Shared memory ID for num_consumed
 * @param shmid_sems Shared memory ID for semaphores
 * @param shmid_stack Shared memory ID for the queue
 */
void cleanup_resources(int *num_produced, int *num_consumed, sem_t *sems, struct int_stack *queue, int shmid_num_produced, int shmid_num_consumed, int shmid_sems, int shmid_stack) {
    // Detach shared memory segments
    shmdt(num_produced);
    shmdt(num_consumed);
    shmdt(sems);
    shmdt(queue);

    // Remove shared memory segments
    shmctl(shmid_num_produced, IPC_RMID, NULL);
    shmctl(shmid_num_consumed, IPC_RMID, NULL);
    shmctl(shmid_sems, IPC_RMID, NULL);
    shmctl(shmid_stack, IPC_RMID, NULL);

    // Destroy semaphores
    for (int i = 0; i < 5; i++) {
        sem_destroy(&sems[i]);
    }
}

/**
 * @brief Producer function that fetches image fragments from servers.
 * 
 * This function runs in a loop, fetching image fragments until all 50 fragments
 * have been produced. It uses semaphores for synchronization with consumers.
 * 
 * @param N The image number to fetch
 * @param num_produced Pointer to the shared counter for produced items
 * @param queue Pointer to the shared queue for storing fragments
 * @param sems Pointer to the array of semaphores
 */
void producer(int N, int *num_produced, struct int_stack *queue, sem_t *sems) {
    // Assign semaphores to more descriptive names
    sem_t* num_produced_mutex = &sems[0];
    sem_t* num_free_spaces_sem = &sems[1];
    sem_t* num_items_sem = &sems[2];
    sem_t* buffer_mutex = &sems[3];

    int server_num = 1;
    
    // Initialize cURL
    curl_global_init(CURL_GLOBAL_DEFAULT);

    char url[256];

    while (true) {
        // Lock update to num_produced with a mutex so that only
        // one producer will fetch a certain fragment number (unique url)
        sem_wait(num_produced_mutex);
        
        // Check if all fragments have been produced
        if (*num_produced >= 50) {
            sem_post(num_produced_mutex);
            break;
        }

        // Prepare URL for the next fragment
        memset(url, '\0', sizeof(url));
        RECV_BUF recv_buf;
        sprintf(url, "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d", server_num, N, *num_produced);
        
        // Increment the number of produced items
        *num_produced += 1;
        sem_post(num_produced_mutex);

        // Initialize buffer for receiving data
        recv_buf_init(&recv_buf, BUF_SIZE);

        // Perform the cURL request
        if (perform_curl_request(url, &recv_buf) != 0) {
            fprintf(stderr, "Failed to perform cURL request\n");
        }

        // Wait for a free space in the buffer
        sem_wait(num_free_spaces_sem);
        
        // Lock the buffer for writing
        sem_wait(buffer_mutex);
        
        // Add the received fragment to the queue
        push(queue, recv_buf);
        
        // Release the buffer lock
        sem_post(buffer_mutex);
        
        // Signal that a new item is available
        sem_post(num_items_sem);

        // Clean up the receive buffer
        recv_buf_cleanup(&recv_buf);
        
        // Cycle through servers
        server_num = (server_num % 3) + 1;
    }
}

/**
 * @brief Consumer function that processes image fragments.
 * 
 * This function runs in a loop, consuming image fragments from the shared queue
 * until all 50 fragments have been consumed. It uses semaphores for synchronization
 * with producers.
 * 
 * @param sleep_time Time to sleep after processing each fragment (in microseconds)
 * @param queue Pointer to the shared queue for storing fragments
 * @param sems Pointer to the array of semaphores
 * @param num_consumed Pointer to the shared counter for consumed items
 */
void consumer(int sleep_time, struct int_stack* queue, sem_t* sems, int* num_consumed)
{
    // Assign semaphores to more descriptive names
    sem_t* num_consumed_mutex = &sems[4];
    sem_t* num_free_spaces_sem = &sems[1];
    sem_t* num_items_sem = &sems[2];
    sem_t* buffer_mutex = &sems[3];

    while (true) {
        // Lock update to num_consumed with a mutex
        sem_wait(num_consumed_mutex);
        
        // Check if all fragments have been consumed
        if (*num_consumed >= 50) {
            sem_post(num_consumed_mutex);
            break;
        }
        
        // Increment the number of consumed items
        *num_consumed += 1;
        sem_post(num_consumed_mutex);

        char fname[256];
        RECV_BUF pop_buf;
        recv_buf_init(&pop_buf, BUF_SIZE);

        // Wait for an item to be available in the buffer
        sem_wait(num_items_sem);

        // Lock the buffer for reading
        sem_wait(buffer_mutex);
        
        // Remove a fragment from the queue
        pop(queue, &pop_buf);
        
        // Release the buffer lock
        sem_post(buffer_mutex);

        // Simulate processing time
        usleep(sleep_time);
        
        // Save the fragment to a file
        sprintf(fname, "./_tmp/%d.png", pop_buf.seq+1);
        write_file(fname, pop_buf.buf, pop_buf.size);
        
        // Clean up the receive buffer
        recv_buf_cleanup(&pop_buf);

        // Signal that a new space is available in the buffer
        sem_post(num_free_spaces_sem);
    }
}


/**
 * @brief Main function to handle inputs, create processes, and manage the producer-consumer problem.
 * 
 * This function parses command-line arguments, sets up shared memory and semaphores,
 * creates producer and consumer processes, waits for their completion, and then
 * concatenates the gathered image fragments.
 * 
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, negative value on error
 */
int main(int argc, char **argv) {
    int status;

    // Check for correct number of arguments
    if ( argc < 6 ) {
        fprintf(stderr, "Usage: %s B P C X N\n", argv[0]);
        exit(1);
    }

    // Parse command-line arguments
    int B = atoi(argv[1]);  // num fragments stored in buffer
    int P = atoi(argv[2]);  // num producers
    int C = atoi(argv[3]);  // num consumers
    int X = atoi(argv[4]);  // consumer sleep time in ms
    int N = atoi(argv[5]);  // image number

    const int NUM_CHILDREN = P + C;
    pid_t pid;
    pid_t cpids[NUM_CHILDREN];  // store all child pids
    double times[2];            // for time count
    struct timeval tv;          // for time count
    unsigned int sleep_time = 1000*X;

    // Start timer
    if (gettimeofday(&tv, NULL) != 0) {
         perror("gettimeofday");
         abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    // Declare shared memory pointers
    int* num_produced;      // shared num produced counter
    int* num_consumed;      // shared num consumed counter
    sem_t* sems;            // all the semaphores
    struct int_stack *queue;// storing B numbers of fragments
    int shm_stack_size = sizeof_shm_stack(B); // size of the stack

    // Allocate shared memory
    int shmid_num_produced = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    int shmid_num_consumed = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    int shmid_sems = shmget(IPC_PRIVATE, sizeof(sem_t) * NUM_SEMS, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
    int shmid_stack = shmget(IPC_PRIVATE, shm_stack_size, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

    // Link shared memory regions to pointers
    num_produced = (int*)shmat(shmid_num_produced, NULL, 0);
    num_consumed = (int*)shmat(shmid_num_consumed, NULL, 0);
    sems = (sem_t*)shmat(shmid_sems, NULL, 0);
    queue = (struct int_stack*)shmat(shmid_stack, NULL, 0);

    // Initialize shared memory segments
    *num_produced = 0;
    *num_consumed = 0;
    if (init_shm_stack(queue, B) > 0) {
        printf("Unable to initialize stack\n");
    }

    // Initialize semaphores
    status  = sem_init(&sems[0], SHARED_SEM, 1); // num_produced mutex
    status += sem_init(&sems[4], SHARED_SEM, 1); // num_consumed mutex
    status += sem_init(&sems[1], SHARED_SEM, B); // does buffer have space semaphore
    status += sem_init(&sems[2], SHARED_SEM, 0); // does buffer have items semaphore
    status += sem_init(&sems[3], SHARED_SEM, 1); // buffer critical mutex

    if (status != 0) {
        perror("sem_init");
        abort();
    }

    // Create producer and consumer processes
    for (int child_i = 0; child_i < NUM_CHILDREN; child_i++) {
        pid = fork();

        if (pid > 0) {
            cpids[child_i] = pid;
        } else if (pid == 0 && child_i < P) { // Producer process
            producer(N, num_produced, queue, sems);

            // Detach from shared memory
            shmdt(sems);
            shmdt(queue);
            shmdt(num_produced);
            shmdt(num_consumed);
            
            // Cleanup cURL
            curl_global_cleanup();

            exit(0);
        } else if (pid == 0 && child_i >= P) { // Consumer process
            consumer(sleep_time, queue, sems, num_consumed);
            
            // Detach from shared memory
            shmdt(num_produced);
            shmdt(num_consumed);
            shmdt(queue);
            shmdt(sems);

            exit(0);
        } else {
            perror("fork");
            abort();
        }
    }

    // Parent process
    // Wait for all producers and consumers to finish
    for (int i = 0; i < NUM_CHILDREN; i++ ) {
        waitpid(cpids[i], &status, 0);
        if (!WIFEXITED(status)) {
            printf("Child cpid[%d]=%d terminated with state: %d.\n", i, cpids[i], status);
        }
    }

    // Concatenate gathered fragments
    // printf("Concatenating fragments...\n");
    char** fragment_files = malloc(50 * sizeof(char*));
    for(int i = 0; i < 50; i++){
        fragment_files[i] = malloc(256 * sizeof(char));
        sprintf(fragment_files[i], "./_tmp/%d.png", i+1);
    }

    // Concatenate all the fragments
    concatenate_pngs(fragment_files, 50);

    // Clean up
    // printf("Cleaning up...\n");
    for(int i = 0; i < 50; i++){
        remove(fragment_files[i]);
        free(fragment_files[i]);
    }
    free(fragment_files);

    // Clean up shared resources
    cleanup_resources(num_produced, num_consumed, sems, queue, shmid_num_produced, shmid_num_consumed, shmid_sems, shmid_stack);

    // Stop timer and print execution time
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("paster2 execution time: %.2lf seconds\n", times[1] - times[0]);
    
    return 0;
}
