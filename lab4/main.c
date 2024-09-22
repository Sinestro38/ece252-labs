#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <time.h>
#include <search.h>
#include "queue.c"

#define SEED_URL "http://ece252-1.uwaterloo.ca/lab4/"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576  /* 1024*1024 = 1M */
#define BUF_INC  524288   /* 1024*512  = 0.5M */

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9
#define BUFFER_SIZE 10
#define HASH_TAB_SIZE 10000

#define max(a, b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

typedef struct recv_buf2 { 
    char *buf;       /* memory to hold a copy of received data */
    size_t size;     /* size of valid data in buf in bytes*/
    size_t max_size; /* max capacity of buf in bytes*/
    int seq;         /* >=0 sequence number extracted from http header */
                     /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct {
    int thread_id;
    int num_threads;
    int max_urls;
    FILE *log_file;
} thread_data;

pthread_mutex_t frontier_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t visited_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t output_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t count_lock;
pthread_cond_t count_threshold_cv;

pthread_cond_t cond = PTHREAD_COND_INITIALIZER;

Queue* frontier_queue;
char *url_frontier[10000];
int frontier_count = 0;
int visited_count = 0;
int png_count = 0;
int reg_count = 0;
int wait_thread_counter = 0;
//Temp
int max_png = 0;

char *visited_urls[10000];
char *png_urls[50];

htmlDocPtr mem_getdoc(char *buf, int size, const char *url);
xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath);
void add_url_to_frontier(const char *url);
void add_url_to_visited(const char *url);
int url_visited(const char *url);
size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int recv_buf_init(RECV_BUF *ptr, size_t max_size);
int recv_buf_cleanup(RECV_BUF *ptr);
void cleanup(CURL *curl, RECV_BUF *ptr);
int write_file(const char *path, const void *in, size_t len);
CURL *easy_handle_init(RECV_BUF *ptr, const char *url);
int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf);
int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf);
int is_valid_png(const char *data, size_t size);
void *crawl(void *arg);
void find_http(char *buf, int size, const char *base_url);

htmlDocPtr mem_getdoc(char *buf, int size, const char *url)
{
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
              HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        fprintf(stderr, "Document not parsed successfully.\n");
        return NULL;
    }
    return doc;
}

CURL *easy_handle_init(RECV_BUF *ptr, const char *url)
{
    CURL *curl_handle = NULL;

    if ( ptr == NULL || url == NULL) {
        return NULL;
    }

    /* init user defined call back function buffer */
    if ( recv_buf_init(ptr, BUF_SIZE) != 0 ) {
        return NULL;
    }
    /* init a curl session */
    curl_handle = curl_easy_init();

    if (curl_handle == NULL) {
        fprintf(stderr, "curl_easy_init: returned NULL\n");
        return NULL;
    }

    /* specify URL to get */
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);

    /* register write call back function to process received data */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_cb_curl3); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)ptr);

    /* register header call back function to process received header data */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_cb_curl); 
    /* user defined data structure passed to the call back function */
    curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)ptr);

    /* some servers requires a user-agent field */
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(curl_handle, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(curl_handle, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(curl_handle, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(curl_handle, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(curl_handle, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(curl_handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);

    return curl_handle;
}

void cleanup(CURL *curl, RECV_BUF *ptr)
{
        curl_easy_cleanup(curl);
        curl_global_cleanup();
        recv_buf_cleanup(ptr);
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
    void *p = NULL;
    
    if (ptr == NULL) {
        return 1;
    }

    p = malloc(max_size);
    if (p == NULL) {
	return 2;
    }
    
    ptr->buf = p;
    ptr->size = 0;
    ptr->max_size = max_size;
    ptr->seq = -1;              /* valid seq should be positive */
    return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
    if (ptr == NULL) {
	return 1;
    }
    
    free(ptr->buf);
    ptr->size = 0;
    ptr->max_size = 0;
    return 0;
}

size_t write_cb_curl3(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
    size_t realsize = size * nmemb;
    RECV_BUF *p = (RECV_BUF *)p_userdata;
 
    if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
        /* received data is not 0 terminated, add one byte for terminating 0 */
        size_t new_size = p->max_size + max(BUF_INC, realsize + 1);   
        char *q = realloc(p->buf, new_size);
        if (q == NULL) {
            perror("realloc"); /* out of memory */
            return -1;
        }
        p->buf = q;
        p->max_size = new_size;
    }

    memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
    p->size += realsize;
    p->buf[p->size] = 0;

    return realsize;
}

size_t header_cb_curl(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
    int realsize = size * nmemb;
    RECV_BUF *p = userdata;

#ifdef DEBUG1_
    //printf("%s", p_recv);
#endif /* DEBUG1_ */
    if (realsize > strlen(ECE252_HEADER) &&
	strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

        /* extract img sequence number */
	p->seq = atoi(p_recv + strlen(ECE252_HEADER));

    }
    return realsize;
}

xmlXPathObjectPtr getnodeset (xmlDocPtr doc, xmlChar *xpath)
{
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        printf("Error in xmlXPathNewContext\n");
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        printf("Error in xmlXPathEvalExpression\n");
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        printf("No result\n");
        return NULL;
    }
    return result;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    //int follow_relative_link = 1;
    char *url = NULL; 

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &url);
    find_http(p_recv_buf->buf, p_recv_buf->size, url); 
    return 0;
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);

    if ( eurl != NULL) {
        pthread_mutex_lock(&output_lock);

        for (int i = 0; i < png_count; i++) {
            if (strcmp(png_urls[i], eurl) == 0) {
                return 0;
            }
        }

        // U8 sig[PNG_SIG_SIZE];

        // memcpy(sig, p_recv_buf->buf, PNG_SIG_SIZE);
        //int png_signature_is_correct = is_png(sig, PNG_SIG_SIZE);
        
        int png_signature_is_correct = is_valid_png(p_recv_buf->buf, p_recv_buf->size);
        
        if(!png_signature_is_correct) {
            return 0;
        }

        if (png_count < max_png) { 
            //strcpy(png_urls[png_count], eurl);
            png_urls[png_count] = strdup(eurl);
            if (png_urls[png_count]) {
                png_count++;
            }
        }

        pthread_mutex_unlock(&output_lock);
    }

    return 0;
}

int process_data(CURL *curl_handle, RECV_BUF *p_recv_buf)
{
    CURLcode res;
    long response_code;

    res = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
	    printf("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) { 
    	fprintf(stderr, "Error.\n");
        return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(curl_handle, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    	printf("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
        fprintf(stderr, "Failed obtain Content-Type\n");
        return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
        return process_html(curl_handle, p_recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
        printf("PNG\n");
        return process_png(curl_handle, p_recv_buf);
    }

    return 0;
}


void add_url_to_frontier(const char *url) {

    printf("Frontier: %d\n", queue_size(frontier_queue));    
    pthread_mutex_lock(&frontier_lock);

    // if (frontier_count == BUFFER_SIZE) {
    //     printf("waiting\n");
    //     pthread_cond_wait(&cond, &frontier_lock);
    // }

    if (!queue_contains(frontier_queue, url)) {
        char *url_copy = strdup(url);
        if(url_copy) {
            queue_push(frontier_queue, url);
        }
    }

    printf("passed\n");

    // pthread_cond_signal(&cond);
    pthread_mutex_unlock(&frontier_lock);
}

void add_url_to_visited(const char *url) {
    //pthread_mutex_lock(&visited_lock);
    //visited_urls[visited_count++] = strdup(url);
    ENTRY add_url;
    add_url.key = strdup(url);
    add_url.data = NULL;

    if(hsearch(add_url, ENTER)){
        fprintf(stderr, "entry failed\n");
        free((void*)add_url.key);
    } else {
        // visited_urls[visited_count] = strdup(url);
        // visited_count++; 
        printf("Add to Visited\n");
    }

    //pthread_mutex_unlock(&visited_lock);
}

int url_visited(const char *url) {
    int visited = 0;
    //pthread_mutex_lock(&visited_lock);
    ENTRY curr_url; 
    ENTRY *url_found;
    curr_url.key = (char *)url;
    curr_url.data = NULL;
    url_found = hsearch(curr_url, FIND);
    //pthread_mutex_unlock(&visited_lock);

    if (url_found) {
        visited = 1;
        printf("FOUND\n");
    }

    return visited;
}

void find_http(char *buf, int size, const char *base_url) {
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;

    if (buf == NULL) {
        return;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset(doc, xpath);
    if (result) {
        printf("test3\n");
        nodeset = result->nodesetval;
        for (i = 0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            xmlChar *old = href;
            href = xmlBuildURI(href, (xmlChar *) base_url);
            xmlFree(old);

            if (href != NULL && !strncmp((const char *)href, "http", 4)) {
                if (url_visited((const char *)href)) {
                    continue;
                }
                else {
                    add_url_to_frontier((const char *)href);
                }
            }

            xmlFree(href);
        }
        xmlXPathFreeObject(result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
}

int is_valid_png(const char *data, size_t size) {
    const uint8_t png_signature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8) {
        return 0;
    }
    return memcmp(data, png_signature, 8) == 0;
}

void* crawl(void *arg) {
    thread_data *data = (thread_data *)arg;
    CURL *curl_handle;
    CURLcode res;
    RECV_BUF recv_buf;

    while (true) {
        pthread_mutex_lock(&frontier_lock);
        
        if (png_count >= max_png || queue_is_empty(frontier_queue)) {
            if (wait_thread_counter < data->num_threads - 1) {
                wait_thread_counter++;
                pthread_cond_wait(&count_threshold_cv, &frontier_lock);
                wait_thread_counter--;
            } else {
                pthread_cond_broadcast(&count_threshold_cv);
                pthread_mutex_unlock(&frontier_lock);
                break;
            }
        }
        
        char *url = queue_pop(frontier_queue);
        add_url_to_visited(url);
        
        pthread_mutex_unlock(&frontier_lock);
        
        // Process URL...

        curl_handle = easy_handle_init(&recv_buf, url);
        if (curl_handle == NULL) {
            free(url);
            continue;
        }

        res = curl_easy_perform(curl_handle);
        if( res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }

        process_data(curl_handle, &recv_buf);

        cleanup(curl_handle, &recv_buf);
        free(url);
        printf("Counter: %i\n", png_count);

        // After processing, if new URLs were found:
        pthread_mutex_lock(&frontier_lock);
        // Add new URLs to frontier
        pthread_cond_signal(&count_threshold_cv);
        pthread_mutex_unlock(&frontier_lock);
    }

    return NULL;
}

int main(int argc, char **argv) {
    int num_threads = 1;
    int max_urls = 50;
    char *log_file_name = NULL;
    FILE *log_file = NULL;
    char *seed_url = SEED_URL;

    int opt;
    while ((opt = getopt(argc, argv, "t:m:v:")) != -1) {
        switch (opt) {
            case 't':
                num_threads = atoi(optarg);
                break;
            case 'm':
                max_urls = atoi(optarg);
                break;
            case 'v':
                log_file_name = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-t num_threads] [-m max_urls] [-v log_file] seed_url\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind < argc) {
        seed_url = argv[optind];
    }

    if (log_file_name) {
        log_file = fopen(log_file_name, "w");
        if (log_file == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }
    }

    curl_global_init(CURL_GLOBAL_ALL);

    if (hcreate(HASH_TAB_SIZE) == 0) {
        perror("hcreate");
        exit(EXIT_FAILURE);
    }

    frontier_queue = queue_create();

    queue_push(frontier_queue, seed_url);

    pthread_t threads[num_threads];
    thread_data data = { .num_threads = num_threads, .max_urls = max_urls, .log_file = log_file };

    max_png = max_urls;

    for (int i = 0; i < num_threads; i++) {
        data.thread_id = i;
        printf("Thread: %d\n", i);
        pthread_create(&threads[i], NULL, crawl, &data);
    }

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    queue_destroy(frontier_queue);
    
    FILE *output_file = fopen("png_urls.txt", "w");
    if (output_file) {
        for (int i = 0; i < png_count; i++) {
            fprintf(output_file, "%s\n", png_urls[i]);
            free(png_urls[i]);
        }
        fclose(output_file);
    }

    if (log_file) {
        fclose(log_file);
    }

    curl_global_cleanup();

    return 0;
}
