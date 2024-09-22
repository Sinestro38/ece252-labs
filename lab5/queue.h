#ifndef QUEUE_H
#define QUEUE_H
#include <stdbool.h>
#include <pthread.h>

#define MAX_URL_LENGTH 256

typedef struct Node {
    char url[MAX_URL_LENGTH];
    struct Node* next;
} Node;

struct Queue {
    Node* front;
    Node* rear;
    int size;
    pthread_mutex_t mutex;
};

typedef struct Queue Queue;

Queue* queue_create();
void queue_destroy(Queue* queue);
void queue_push(Queue* queue, const char* url);
char* queue_pop(Queue* queue);
bool queue_is_empty(Queue* queue);
bool queue_contains(Queue* queue, const char* url);
int queue_size(Queue* queue);

#endif // QUEUE_H