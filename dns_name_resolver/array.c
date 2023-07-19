#include "array.h"

sem_t space_avail;
sem_t items_avail;
sem_t mutex;

int array_init(array *s, int arr_size) {                  
	// init the array and top
	int i;
	s->array = (char**)malloc(arr_size * sizeof(char*));
	for(i = 0; i < arr_size; i++) {
		s->array[i] = (char*)malloc(MAX_NAME_LENGTH * sizeof(char));
	}
	s->top = 0;
	// init semaphores
	sem_init(&space_avail, 0, arr_size);
	sem_init(&items_avail, 0, 0);
	sem_init(&mutex, 0, 1);
 	return 0;
}

int array_put(array *s, char *hostname) {     // place element on the top of the stack
   
	sem_wait(&space_avail);
	sem_wait(&mutex);
	strcpy(s->array[s->top++], hostname);
	sem_post(&mutex);
	sem_post(&items_avail);

	return 0;
}

int array_get(array *s, char **hostname) {     // remove element from the top of the stack
	
	sem_wait(&items_avail);
	sem_wait(&mutex);      
	strcpy(*hostname, s->array[--s->top]);
	sem_post(&mutex);
	sem_post(&space_avail);

	return 0;
}

int array_top(array *s) {
	int t;
	sem_wait(&mutex);
	t = s->top;
	sem_post(&mutex);
	return t;
}

void array_free(array *s, int arr_size) {                 
	// free the array
	int i;
	for (i = 0; i < arr_size; i++) {
		free(s->array[i]);
	}
	free(s->array);
	// destroy semaphores
	sem_destroy(&space_avail);
	sem_destroy(&items_avail);
	sem_destroy(&mutex);
}