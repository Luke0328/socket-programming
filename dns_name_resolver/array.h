#ifndef ARRAY_H 
#define ARRAY_H

#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>
#include <string.h>

#define MAX_NAME_LENGTH 50
#define ARRAY_SIZE 20

typedef struct {
	char **array;
	int top;
} array;

int  array_init(array *s, int arr_size); // initialize the array
int  array_put (array *s, char *hostname); // place element into the array
int  array_get (array *s, char **hostname); // remove element from the array
int  array_top (array *s); // access top safely
void array_free(array *s, int arr_size); // free the array's resources

#endif