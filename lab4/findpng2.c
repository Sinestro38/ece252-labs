/**
 * @file findpng2.c
 * @brief A multi-threaded web crawler to find PNG images.
 *
 * This program crawls web pages starting from a seed URL, searching for PNG images.
 * It uses multiple threads to improve performance and can be configured with
 * command-line arguments to specify the number of threads, maximum number of
 * PNG images to find, and an optional log file.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <search.h>
#include <unistd.h>
#include <semaphore.h>
#include <pthread.h>
#include "http_utils.h"
#include "queue.h"

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

int png_cnt = 0;
int url_cnt = 0;
int tosee_cnt = 1;
int num_threads = 1;
int num_pngs_target = 50;
int check_logfile = 0;
char logfile[256];
int wait_cnt = 0;
Queue *tosee_queue;
struct hsearch_data *visited_list;
Queue *png_queue;
Queue *log_queue;

char* elements_popped[2000];
atomic_int elements_popped_index;

pthread_mutex_t tosee_lock;
pthread_mutex_t png_lock;
pthread_mutex_t log_lock;
pthread_rwlock_t visited_lock;
pthread_cond_t tosee_cond;

// Function prototypes
void hash_table_insert(struct hsearch_data *tab, char *key);
int hash_table_find(struct hsearch_data *tab, char *key);

int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf);
int is_valid_png(RECV_BUF *p_recv_buf);
void* crawl(void *arg);


/**
 * @brief Inserts a key into the hash table.
 * @param tab Pointer to the hash table.
 * @param key The key to insert.
 */
void hash_table_insert(struct hsearch_data *tab, char *key) {
    ENTRY item = {key};
    ENTRY *pitem = &item;
    hsearch_r(item, ENTER, &pitem, tab);
}

/**
 * @brief Searches for a key in the hash table.
 * @param tab Pointer to the hash table.
 * @param key The key to search for.
 * @return 0 if found, -1 if not found.
 */
int hash_table_find(struct hsearch_data *tab, char *key) {
    ENTRY item = {key};
    ENTRY *pitem = &item;
    return hsearch_r(item, FIND, &pitem, tab) ? 0 : -1;
}

/**
 * @brief Finds HTTP links in the given buffer.
 * @param buf The buffer containing HTML content.
 * @param size Size of the buffer.
 * @param follow_relative_links Whether to follow relative links.
 * @param base_url The base URL for resolving relative links.
 * @return 0 on success, 1 on failure.
 */
int find_http(char *buf, int size, int follow_relative_links, const char *base_url) {
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
        
    if (buf == NULL) {
        return 1;  // return failure if buffer is null
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset(doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if (follow_relative_links) { // if follow_relative_links is true, then follow the relative link
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if (href != NULL && !strncmp((const char *)href, "http", 4)) {
                // check if the href is already visited
                pthread_rwlock_rdlock(&visited_lock);
                int find = hash_table_find(visited_list, (char*)href);
                pthread_rwlock_unlock(&visited_lock);
               
                if (find == -1) {  // if the href is not in the visited list
                    pthread_mutex_lock(&tosee_lock);
                    if (!queue_contains(tosee_queue, (char*)href)) {
                        // add new url to the queue
                        queue_push(tosee_queue, (char*)href);
                        tosee_cnt++;
                        pthread_cond_signal(&tosee_cond);
                    }
                    pthread_mutex_unlock(&tosee_lock);
                }
            }
              
            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    
    return 0;  // return success
}

/**
 * @brief Checks if the received buffer contains a PNG image.
 * @param p_recv_buf Pointer to the received buffer.
 * @return 1 if PNG, 0 otherwise.
 */
int is_valid_png(RECV_BUF *p_recv_buf) {
    if (p_recv_buf == NULL || p_recv_buf->buf == NULL || p_recv_buf->size < 8) {
        return 0;
    }
    unsigned char png_format[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(png_format, p_recv_buf->buf, 8) == 0;
}

/**
 * @brief Processes HTML content.
 * @param curl_handle The CURL handle.
 * @param p_recv_buf Pointer to the received buffer.
 * @return 0 on success.
 */
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    int follow_relative_link = 1;
    char *url = NULL; 
   
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 

    return 0;
}

/**
 * @brief Processes PNG content.
 * @param curl_handle The CURL handle.
 * @param p_recv_buf Pointer to the received buffer.
 * @return 1 if PNG processed, 0 otherwise.
 */
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (eurl != NULL && is_valid_png(p_recv_buf)) {
        pthread_mutex_lock(&png_lock);
        if (png_cnt < num_pngs_target) {
            queue_push(png_queue, eurl);
            png_cnt++;
        }
        pthread_mutex_unlock(&png_lock);
       
        return 1;
    }
   
    return 0;
}

/**
 * @brief Processes the downloaded data.
 * @param curl_handle The CURL handle.
 * @param p_recv_buf Pointer to the received buffer.
 * @return 0 on success, non-zero otherwise.
 */
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    CURLcode res;
    long response_code;
    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (res == CURLE_OK) {
        // Response code retrieved successfully
    }

    if (response_code >= 400) { 
        return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if (res == CURLE_OK && ct != NULL) {
        // Content type retrieved successfully
    } else {
        return 2;
    }

    if (strstr(ct, CT_HTML)) {
        return process_html(curl_handle, p_recv_buf);
    } else if (strstr(ct, CT_PNG)) {
        return process_png(curl_handle, p_recv_buf);
    } else {
        // Unhandled content type
    }

    return 0;
}

/**
 * @brief The main crawler function executed by each thread.
 * @param ignore Unused parameter.
 * @return NULL.
 */
void *crawler(void *ignore) {
    // curl handle for performing HTTP requests
    CURL *curl_handle;
    // result code for curl operations
    CURLcode res;
    // buffer for receiving data from HTTP requests
    RECV_BUF recv_buf;
    // num_pngs_founder for PNG files found
    int num_pngs_found = 0;

    while (1) {
        // check if we've reached the desired number of PNG files
        pthread_mutex_lock(&png_lock);
        num_pngs_found = png_cnt;
        pthread_mutex_unlock(&png_lock);

        if (num_pngs_found >= num_pngs_target) {
            // signal that this thread is done and wait for others
            pthread_mutex_lock(&tosee_lock);
            wait_cnt++;
            if (wait_cnt == num_threads) {
                pthread_cond_broadcast(&tosee_cond);
            }
            pthread_mutex_unlock(&tosee_lock);
            break;
        }

        if (num_pngs_found < num_pngs_target) {
            pthread_mutex_lock(&tosee_lock);

            // wait if the queue is empty or other threads are waiting
            if (queue_is_empty(tosee_queue) || wait_cnt != 0) {
                wait_cnt++;
                if (wait_cnt < num_threads) {
                    pthread_cond_wait(&tosee_cond, &tosee_lock);
                }

                // recheck PNG num_pngs_found after waiting
                pthread_mutex_lock(&png_lock);
                num_pngs_found = png_cnt;
                pthread_mutex_unlock(&png_lock);

                // break if all threads are waiting and conditions are met
                if ((wait_cnt == num_threads && queue_is_empty(tosee_queue)) || 
                    (wait_cnt == num_threads && num_pngs_found >= num_pngs_target)) {
                    pthread_cond_broadcast(&tosee_cond);
                    pthread_mutex_unlock(&tosee_lock);
                    break;
                }
                wait_cnt--;
            }

            // get next URL from the queue
            char* element = queue_pop(tosee_queue);
            elements_popped[elements_popped_index++] = element;
            tosee_cnt--;
            pthread_mutex_unlock(&tosee_lock);

            if (element == NULL) {
                continue;
            }

            // check if URL has been visited
            pthread_rwlock_wrlock(&visited_lock);
            if (hash_table_find(visited_list, element) != -1) {   
                pthread_rwlock_unlock(&visited_lock);
                continue;
            }
            hash_table_insert(visited_list, element);
            url_cnt++;
            pthread_rwlock_unlock(&visited_lock);

            // log URL if logging is enabled
            pthread_mutex_lock(&log_lock);
            if (check_logfile) {
                queue_push(log_queue, element);
            }
            pthread_mutex_unlock(&log_lock);

            // initialize curl handle and perform request
            curl_handle = easy_handle_init(&recv_buf, element);
            if (curl_handle == NULL) {
                curl_global_cleanup();
                abort();
            }
            res = curl_easy_perform(curl_handle);

            if (res != CURLE_OK) {
                // handle curl error (not implemented)
            } else {
                // process the received data
                process_data(curl_handle, &recv_buf);
            }

            // clean up curl handle and received buffer
            cleanup(curl_handle, &recv_buf);
        }
    }
    return NULL;
}

/**
 * @brief Prints the contents of a queue to a file.
 * @param queue The queue to print.
 * @param filename The name of the file to write to.
 */
void print_queue_to_file(Queue* queue, const char* filename) {
    FILE* fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("Unable to open file!\n");
        return;
    }

    pthread_mutex_lock(&queue->mutex);
    Node* current = queue->front;
    while (current != NULL) {
        fprintf(fp, "%s\n", current->url);
        current = current->next;
    }
    pthread_mutex_unlock(&queue->mutex);

    fclose(fp);
}

/**
 * @brief The main function of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return 0 on success, -1 on error.
 */
int main(int argc, char** argv) {
    int c;
    int arg_num = 0;
    char url[256];
    char *str = "option requires an argument";

    while ((c = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (c) {
            case 't':
                num_threads = strtoul(optarg, NULL, 10);
                arg_num++;
                
                if (num_threads <= 0) {
                    fprintf(stderr, "%s: %s > 0 -- 't'\n", argv[0], str);
                    return -1;
                }
                break;
            case 'm':
                num_pngs_target = strtoul(optarg, NULL, 10);
                arg_num++;
                if (num_pngs_target <= 0) {
                    fprintf(stderr, "%s: %s > 0 -- 'm'\n", argv[0], str);
                    return -1;
                }
                break;
            case 'v':
                strcpy(logfile, optarg);
                check_logfile = 1;
                arg_num++;
                if (strstr(logfile, ".txt") == NULL) {
                    fprintf(stderr, "%s: %s should be a txt file -- 'v'\n", argv[0], str);
                }
                break;
            default:
                return -1;
        }
    }
    
    if (argc % 2 == 0) {
        strcpy(url, argv[arg_num * 2 + 1]);
    } else {
        strcpy(url, SEED_URL); 
    }

    pthread_t web_crawler[num_threads];
    tosee_queue = queue_create();
    visited_list = calloc(1, sizeof(struct hsearch_data));
    png_queue = queue_create();
    log_queue = queue_create();
    hcreate_r(500, visited_list);
    pthread_mutex_init(&tosee_lock, NULL);
    pthread_mutex_init(&png_lock, NULL);
    pthread_mutex_init(&log_lock, NULL);
    pthread_rwlock_init(&visited_lock, NULL);
    pthread_cond_init(&tosee_cond, NULL);

    struct timeval tv;
    double times[2];
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    queue_push(tosee_queue, url);

    if(num_threads == 1){
        crawler(NULL);
    }
    else{
        for(int i = 0; i < num_threads; i++){
            pthread_create(&web_crawler[i], NULL, crawler, NULL);
        }
        for(int j = 0; j < num_threads; j++){
            pthread_join(web_crawler[j], NULL);
        }
    }

    for (int i = 0; i < elements_popped_index; i++) {
        free(elements_popped[i]);
    }

    if(check_logfile) print_queue_to_file(log_queue, logfile);
    print_queue_to_file(png_queue, "png_urls.txt");
    
    queue_destroy(tosee_queue);
    queue_destroy(png_queue);
    queue_destroy(log_queue);
    
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng2 execution time: %.6lf seconds\n", times[1] - times[0]);

    hdestroy_r(visited_list);
    free(visited_list);

    pthread_mutex_destroy(&tosee_lock);
    pthread_mutex_destroy(&png_lock);
    pthread_mutex_destroy(&log_lock);
    pthread_rwlock_destroy(&visited_lock);
    pthread_cond_destroy(&tosee_cond);

    curl_global_cleanup();
    xmlCleanupParser();
    
    return 0;
}