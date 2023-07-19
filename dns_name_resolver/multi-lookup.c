#include "multi-lookup.h"
  
#define NUM_GETS 0

void *requester(void *vargp) 
{ 
    req_args* args = (req_args*)vargp;
    array* arr = args->arr;
	char (*file_arr)[MAX_FILENAME_LENGTH] = args->file_arr;
    pthread_mutex_t *file_lock = args->file_arr_lock;
    int *f_top = args->f_top;
	FILE *req_log = args->req_log;
	pthread_mutex_t *req_log_lock = args->req_log_lock;
	int *num_req = args->num_req;
	pthread_mutex_t *num_req_lock = args->num_req_lock;

    int file_count = 0;

    while (1) {
		pthread_mutex_lock(file_lock); // lock file array

		if (*f_top > 0) {

        	char filename[50];

			(*f_top)--;

			strcpy(filename, file_arr[*f_top]);

			pthread_mutex_unlock(file_lock); // unlock
			
			file_count++;

			FILE *file = fopen(filename, "r"); // open file
			if(file == NULL) {
				file_count--;
				fprintf(stderr, "Invalid file %s\n", filename);
			}
			else {
				char line[MAX_NAME_LENGTH];

				// read file line by line
				while(fgets(line, MAX_NAME_LENGTH, file)) {
					line[strcspn(line, "\n")] = 0; // remove trailing newline
					array_put(arr, line); // put hostname in my_stack
					pthread_mutex_lock(req_log_lock); 
					fprintf(req_log, "%s\n", line); // write hostname to requester log
					pthread_mutex_unlock(req_log_lock);
				}
			}
			fclose(file);
		}
		else {
			pthread_mutex_unlock(file_lock); // unlock file array
			break;
		}
    }
	
    printf("thread %ld serviced %d files\n", pthread_self(), file_count);
	
	pthread_mutex_lock(num_req_lock);
	(*num_req)--; // decrement the number of active requester threads before exiting
	pthread_mutex_unlock(num_req_lock);

    return NULL; // exit thread
} 

void *resolver(void *vargp) 
{ 
    res_args* args = (res_args*)vargp;
    array* arr = args->arr;
	FILE *res_log = args->res_log;
	pthread_mutex_t *res_log_lock = args->res_log_lock;    
	int *num_req = args->num_req;
	pthread_mutex_t *num_req_lock = args->num_req_lock;

	char name[MAX_NAME_LENGTH];
	char *hostname = name;
	char tmp[MAX_IP_LENGTH];
	char *ip = tmp;

	int num_hostnames = 0;

	while(1) {
		
		pthread_mutex_lock(num_req_lock);
		if((*num_req) > 0 || array_top(arr) > 0) {
			pthread_mutex_unlock(num_req_lock);

			array_get(arr, &hostname);

			if(dnslookup(hostname, ip, MAX_IP_LENGTH) == 0) {
				pthread_mutex_lock(res_log_lock);
				fprintf(res_log, "%s, %s\n", hostname, ip);
				pthread_mutex_unlock(res_log_lock);
				num_hostnames++;
			}
			else {
				pthread_mutex_lock(res_log_lock);
				fprintf(res_log, "%s, NOT_RESOLVED\n", hostname);
				pthread_mutex_unlock(res_log_lock);
			}
		}
		else {
			pthread_mutex_unlock(num_req_lock);
			break;
		}
	}    

    printf("thread %ld resolved %d hostnames\n", pthread_self(), num_hostnames);

    return NULL; // exit thread
} 
   
int main(int argc, char* argv[]) 
{ 

    // START TIMER
	struct timeval start, stop;
	gettimeofday(&start, NULL);

	// CHECK ARGUMENTS
	if (argc < 5) {
		fprintf(stderr, "Not enough arguments\n");
		return -1;
	}
	else if (argc > 5 + MAX_INPUT_FILES) {
		fprintf(stderr, "Too many arguments\n");
		return -1;
	}

	char *reqPtr;
	int num_req = (int)strtol(argv[1], &reqPtr, 10);
	int num_req2 = num_req;
	if(num_req > MAX_REQUESTER_THREADS) {
		fprintf(stderr, "Too many requester threads\n");
		return -1;
	}
	else if(num_req == 0 && strlen(reqPtr) > 0) {
		fprintf(stderr, "String given for number of requester threads\n");
		return -1;
	}
	else if(num_req < 0) {
		fprintf(stderr, "No negative number of requester threads\n");
		return -1;
	}
	
	char *resPtr;
	int num_res = (int)strtol(argv[2], &resPtr, 10);
	if(num_res > MAX_RESOLVER_THREADS) {
		printf("Too many resolver threads\n");
		return -1;
	}
	else if(num_res == 0 && strlen(resPtr) > 0) {
		fprintf(stderr, "String given for number of resolver threads\n");
		return -1;
	}
	else if(num_res < 0) {
		printf("No negative number of resolver threads\n");
		return -1;
	}

    // INIT STACK
    array my_stack;
    if (array_init(&my_stack, ARRAY_SIZE) < 0) {
		fprintf(stderr, "Failed to initialize array");
		exit(-1);
	}

    // INIT FILE ARRAY
    char file_arr[MAX_INPUT_FILES][MAX_FILENAME_LENGTH];

    // POPULATE FILE ARRAY
    int f;
    int f_top = 0;
    for(f = 5; f < argc; f++) {
		strcpy(file_arr[f-5], argv[f]);
        f_top++;
    }


    // INIT MUTEXES
    pthread_mutex_t file_arr_lock;
    if (pthread_mutex_init(&file_arr_lock, NULL) < 0) {
		fprintf(stderr, "Failed to initialize Mutex\n");
 		exit(-1);
	}
    pthread_mutex_t req_log_lock;
    if (pthread_mutex_init(&req_log_lock, NULL) < 0) {
		fprintf(stderr, "Failed to initialize Mutex\n");
 		exit(-1);
	}
    pthread_mutex_t res_log_lock;
    if (pthread_mutex_init(&res_log_lock, NULL) < 0) {
		fprintf(stderr, "Failed to initialize Mutex\n");
 		exit(-1);
	}
    pthread_mutex_t num_req_lock;
    if (pthread_mutex_init(&num_req_lock, NULL) < 0) {
		fprintf(stderr, "Failed to initialize Mutex\n");
 		exit(-1);
	}

    // INIT ARGS FOR REQUESTER
    req_args req_in;
    req_in.arr = &my_stack;
	req_in.file_arr = file_arr;
    req_in.f_top = &f_top;
    req_in.file_arr_lock = &file_arr_lock;
	FILE *req_log = fopen(argv[3], "w");
	if(req_log == NULL) {
		fprintf(stderr, "Failed to open requester log file\n");	
	}
	else {
		req_in.req_log = req_log;
	}
	req_in.req_log_lock = &req_log_lock;
	req_in.num_req = &num_req;
	req_in.num_req_lock = &num_req_lock;

    // INIT ARGS FOR RESOLVER
    res_args res_in;
    res_in.arr = &my_stack;
	FILE *res_log = fopen(argv[4], "w");
	if(req_log == NULL) {
		fprintf(stderr, "Failed to open resolver log file\n");	
	}
	else {
		res_in.res_log = res_log;
	}
	res_in.res_log_lock = &res_log_lock;
	res_in.num_req = &num_req;
	res_in.num_req_lock = &num_req_lock;
    
    // INIT REQUESTERS
    pthread_t p_tid[num_req];
    int i;
    for(i = 0; i < num_req; i++) {
        pthread_create(&p_tid[i], NULL, &requester, (void*)&req_in); 
    }

    // INIT RESOLVERS
    pthread_t c_tid[num_res];
    int j;
    for(j = 0; j < num_res; j++) {
        pthread_create(&c_tid[j], NULL, &resolver, (void*)&res_in); 

    }

	int k;
	// JOIN REQUESTERS AND RESOLVERS
    for(k = 0; k < num_req2; k++) {
        pthread_join(p_tid[k], NULL); 
    }

	int l;
    for(l = 0; l < num_res; l++) {
        pthread_join(c_tid[l], NULL); 
    }

	// CLOSE FILES
	fclose(req_in.req_log);
	fclose(res_in.res_log);

    // FREE MEMORY
    array_free(&my_stack, ARRAY_SIZE);

	// DESTROY MUTEXES
    pthread_mutex_destroy(&file_arr_lock);
    pthread_mutex_destroy(&res_log_lock);
    pthread_mutex_destroy(&req_log_lock);
    pthread_mutex_destroy(&num_req_lock);

	// PRINT TIME TAKEN
	gettimeofday(&stop, NULL);
    printf("total time: %0.8f seconds\n", (stop.tv_sec - start.tv_sec) + 1e-6*(stop.tv_usec - start.tv_usec) );

    exit(0); 
}