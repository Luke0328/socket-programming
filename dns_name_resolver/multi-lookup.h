#ifndef MULTILOOKUP_H 
#define MULTILOOKUP_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>   
#include <time.h>
#include <sys/time.h>

#include "array.h"
#include "util.h"

#define MAX_INPUT_FILES 100
#define MAX_REQUESTER_THREADS 10
#define MAX_RESOLVER_THREADS 10
#define MAX_IP_LENGTH INET6_ADDRSTRLEN
#define MAX_FILENAME_LENGTH 50

typedef struct {
	array *arr;
	char (*file_arr)[MAX_FILENAME_LENGTH];
	int *f_top;
	pthread_mutex_t *file_arr_lock;
	FILE *req_log;
	pthread_mutex_t *req_log_lock;
	int* num_req;
	pthread_mutex_t *num_req_lock;
}req_args;

typedef struct {
	array *arr;
	FILE *res_log;
	pthread_mutex_t *res_log_lock;
	int* num_req;
	pthread_mutex_t *num_req_lock;
}res_args;

void *requester(void *vargp); // producer thread function
void *resolver(void *vargp); // consumer thread function

#endif