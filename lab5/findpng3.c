#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <search.h>
#include <unistd.h>
#include "http_utils.h"
#include "queue.h"

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define MAX_WAIT_MSECS 30*1000

int png_cnt;
int num_pngs_target = 50;
int num_connections = 1;
int check_logfile = 0;
char logfile[256];
Queue *tosee_queue;
Queue *png_queue;
Queue *log_queue;
struct hsearch_data *visited_list;

char* elements_popped[2000];
atomic_int elements_popped_index;

typedef struct curl_info {
  RECV_BUF* buf;
  char* url;
} curl_info;

void hash_table_insert(struct hsearch_data *tab, char *key);
int hash_table_find(struct hsearch_data *tab, char *key);
int find_http(char *fname, int size, int follow_relative_links, const char *base_url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf);
int is_valid_png(RECV_BUF *p_recv_buf);
void print_queue_to_file(Queue* queue, const char* filename);
static void curl_multi_handle_init(CURLM *curl_multi, char* url);

void hash_table_insert(struct hsearch_data *tab, char *key) {
    ENTRY item = {key};
    ENTRY *pitem = &item;
    hsearch_r(item, ENTER, &pitem, tab);
}

int hash_table_find(struct hsearch_data *tab, char *key) {
    ENTRY item = {key};
    ENTRY *pitem = &item;
    return hsearch_r(item, FIND, &pitem, tab) ? 0 : -1;
}

int is_valid_png(RECV_BUF *p_recv_buf) {
    if (p_recv_buf == NULL || p_recv_buf->buf == NULL || p_recv_buf->size < 8) {
        return 0;
    }
    unsigned char png_format[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    return memcmp(png_format, p_recv_buf->buf, 8) == 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL;
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if (eurl != NULL && is_valid_png(p_recv_buf)) {

        if (png_cnt < num_pngs_target) {
            queue_push(png_queue, eurl);
            png_cnt++;
        }
       
        return 1;
    }
   
    return 0;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    int follow_relative_link = 1;
    
    char *url = NULL; 
   
    if (p_recv_buf == NULL || p_recv_buf->buf == NULL) {
        fprintf(stderr, "Error: Receive buffer is null in process_html\n");
        return -1;
    }

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, url); 

    return 0;
}

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
                int find = hash_table_find(visited_list, (char*)href);

                if (find == -1) {  // if the href is not in the visited list
                    if (!queue_contains(tosee_queue, (char*)href)) {
                        // add new url to the queue
                        queue_push(tosee_queue, (char*)href);
                    }
                }
            }
              
            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    
    return 0;  // return success
}

static void curl_multi_handle_init(CURLM *curl_multi, char* url)
{
  RECV_BUF* buf = malloc(sizeof(RECV_BUF));

  CURL *easy_handle = easy_handle_init(buf, url);
  if (!easy_handle) {
    fprintf(stderr, "Curl initialization failed.\n");
    curl_global_cleanup();
    abort();
  }

  curl_info* transfer_info = malloc(sizeof(curl_info));

  transfer_info->buf = buf;
  transfer_info->url = url;

  curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, (void*) transfer_info);

  curl_multi_add_handle(curl_multi, easy_handle);
}

void print_queue_to_file(Queue* queue, const char* filename) {
    
    FILE* fp = fopen(filename, "w");
    if (fp == NULL) {
        printf("Unable to open file!\n");
        return;
    }

    Node* current = queue->front;
    while (current != NULL) {
        fprintf(fp, "%s\n", current->url);
        current = current->next;
    }

    fclose(fp);
}

int main( int argc, char** argv )
{
    int c;
    int arg_num = 0;
    char url[256];
    png_cnt = 0;
    char *str = "option requires an argument";

    while ((c = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (c) {
            case 't':
                num_connections = strtoul(optarg, NULL, 10);
                arg_num++;
                
                if (num_connections <= 0) {
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

    struct timeval tv;
    double times[2];
    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[0] = (tv.tv_sec) + tv.tv_usec/1000000.;

    CURLM *curl_multi = NULL;
    CURL *easy_handle = NULL;
    CURLMsg *curl_message = NULL;
    curl_info* info;
    void* temp = NULL;

    int is_running;
    int transfers;
    int remaining_transfers;
    int http_number;
    int numfds;

    curl_global_init(CURL_GLOBAL_ALL);
    curl_multi = curl_multi_init();

    tosee_queue = queue_create();
    png_queue = queue_create();
    log_queue = queue_create();
    visited_list = calloc(1, sizeof(struct hsearch_data));
    hcreate_r(1000, visited_list);

    queue_push(tosee_queue, url);

    is_running = 0;

    while (!queue_is_empty(tosee_queue) && png_cnt < num_pngs_target) {

        transfers = 0;

        while (transfers < num_connections && !queue_is_empty(tosee_queue)) {
            char* element = queue_pop(tosee_queue);
            elements_popped[elements_popped_index++] = element;

            curl_multi_handle_init(curl_multi, element);
            if (hash_table_find(visited_list, element) == -1) {
                transfers++;
                hash_table_insert(visited_list, element);
            }
            
            if (check_logfile) {
                queue_push(log_queue, element);
            }
        }

        CURLMcode mc = curl_multi_perform(curl_multi, &is_running);
        if (mc != CURLM_OK) {
            fprintf(stderr, "curl_multi_perform() failed, code %d.\n", mc);
            break;
        }

        do {
            numfds = 0;
            int rc = curl_multi_wait(curl_multi, NULL, 0, MAX_WAIT_MSECS, &numfds);
            if (rc != CURLM_OK) {
                fprintf(stderr, "error: curl_multi_wait() returned %d\n", rc);
                return EXIT_FAILURE;
            }
            mc = curl_multi_perform(curl_multi, &is_running);
            if (mc != CURLM_OK) {
                fprintf(stderr, "curl_multi_perform() failed, code %d.\n", mc);
                break;
            }
        } while(is_running > 0);

        remaining_transfers = -1;
        while ((curl_message = curl_multi_info_read(curl_multi, &remaining_transfers))) {
            if (curl_message->msg == CURLMSG_DONE) {
                easy_handle = curl_message->easy_handle;
                http_number = 0;

                int res = curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &http_number);
                curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &temp);

                info = ( curl_info* ) temp;
                
                if (res != CURLE_OK || info == NULL) {
                    fprintf(stderr, "Error: Failed to get RECV_BUF from easy handle\n");
                    continue;
                }

                process_data(easy_handle, info->buf);
                
                curl_multi_remove_handle(curl_multi, easy_handle);
                cleanup(easy_handle, info->buf);
                free(info->buf);
                free(info);
            }
        }
    }

    for (int i = 0; i < elements_popped_index; i++) {
        free(elements_popped[i]);
    }

    if(check_logfile) print_queue_to_file(log_queue, logfile);
    print_queue_to_file(png_queue, "png_urls.txt");   

    curl_multi_cleanup(curl_multi);
    curl_global_cleanup();

    queue_destroy(tosee_queue);
    queue_destroy(png_queue);
    queue_destroy(log_queue);

    if (gettimeofday(&tv, NULL) != 0) {
        perror("gettimeofday");
        abort();
    }
    times[1] = (tv.tv_sec) + tv.tv_usec/1000000.;
    printf("findpng3 execution time: %.6lf seconds\n", times[1] - times[0]);

    hdestroy_r(visited_list);
    free(visited_list);

    return 0;
}