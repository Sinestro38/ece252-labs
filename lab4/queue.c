#include "queue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

Queue* queue_create() {
    Queue* queue = (Queue*)malloc(sizeof(Queue));
    if (queue) {
        queue->front = queue->rear = NULL;
        queue->size = 0;
        pthread_mutex_init(&queue->mutex, NULL);
    }
    return queue;
}

void queue_destroy(Queue* queue) {
    if (!queue) return;

    pthread_mutex_lock(&queue->mutex);
    while (queue->front) {
        Node* temp = queue->front;
        queue->front = queue->front->next;
        free(temp);
    }
    pthread_mutex_unlock(&queue->mutex);

    pthread_mutex_destroy(&queue->mutex);
    free(queue);
}

void queue_push(Queue* queue, const char* url) {
    if (!queue || !url) return;

    Node* new_node = (Node*)malloc(sizeof(Node));
    if (!new_node) return;

    strncpy(new_node->url, url, MAX_URL_LENGTH - 1);
    new_node->url[MAX_URL_LENGTH - 1] = '\0';
    new_node->next = NULL;

    pthread_mutex_lock(&queue->mutex);
    if (queue->rear == NULL) {
        queue->front = queue->rear = new_node;
    } else {
        queue->rear->next = new_node;
        queue->rear = new_node;
    }
    queue->size++;
    pthread_mutex_unlock(&queue->mutex);
}

char* queue_pop(Queue* queue) {
    if (!queue || queue_is_empty(queue)) return NULL;

    pthread_mutex_lock(&queue->mutex);
    Node* temp = queue->front;
    char* url = strdup(temp->url);
    queue->front = queue->front->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    queue->size--;
    free(temp);
    pthread_mutex_unlock(&queue->mutex);

    return url;
}

bool queue_is_empty(Queue* queue) {
    if (!queue) return true;
    return queue->front == NULL;
}

bool queue_contains(Queue* queue, const char* url) {
    if (!queue || !url) return false;

    bool found = false;
    pthread_mutex_lock(&queue->mutex);
    for (Node* current = queue->front; current != NULL; current = current->next) {
        if (strcmp(current->url, url) == 0) {
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&queue->mutex);

    return found;
}

int queue_size(Queue* queue) {
    if (!queue) return 0;
    return queue->size;
}