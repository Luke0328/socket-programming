/* 
* Distributed file client
* usage: client <command> <filenames>
* Parts of code taken from https://www.cs.dartmouth.edu/~campbell/cs50/socketprogramming.html
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h> 
#include <signal.h>
#include <sys/time.h>
#include <sys/sendfile.h>
#include <fcntl.h>

#define BUFSIZE 8192
#define NUM_SRVS 4
#define MAX_NUM_FILES 256
#define MAX_FILENAME_LEN 128

/*
* error - wrapper for perror
*/
void error(char *msg) {
    perror(msg);
    exit(1);
}

int conn_to_servers(int *serversockfds);
void send_all(int connfd, int *sz, char *data);
unsigned long hash(char *str);

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int connfd; /* connection*/
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp; /* client host info */
    char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    pid_t childpid;
    char main_buf[BUFSIZE];

    char cmd[4]; // command - list, get or put
    int serversockfds[NUM_SRVS];

    /* 
    * check command line arguments 
    */
    if (argc < 2) {
        fprintf(stderr, "usage: %s <command> [filename] ... [filename]\n", argv[0]);
        exit(1);
    }
    sprintf(cmd, "%s", argv[1]);

    // decide actions depending on command
    if(strcmp("ls", cmd) == 0) {
        // connect to servers
        conn_to_servers(serversockfds);

        int n;
        bzero(main_buf, BUFSIZE);
        char *buf = malloc(1024);
        char *msg = "LIST";

        for(int i = 0; i < NUM_SRVS; i++) {
            // send cmd to servers
            if(serversockfds[i] == -1) {
                continue;
            }
            int len = strlen(msg);
            send_all(serversockfds[i], &len, msg);

            // recv list of files
            bzero(buf, 1024);
            while( (recv(serversockfds[i], buf, 1024, 0)) > 0 ) {
                strcat(main_buf, buf);
            }
        }
        free(buf);
        
        // buffer to store file names
        char filenames[MAX_NUM_FILES * NUM_SRVS][MAX_FILENAME_LEN];
        int num_files = 0;
        // buffer to record which chunks are present 
        int chunks[MAX_NUM_FILES * NUM_SRVS][4];
        //init to zero
        for (int i = 0; i < MAX_NUM_FILES * NUM_SRVS; i++) {
            for (int j = 0; j < 4; j++) {
                chunks[i][j] = 0;
            }
        }

        int line_start = 0;
        char *line = strtok(main_buf, "\n");
        while (line != NULL) {
            char *linecpy = malloc(strlen(line));
            // FORMAT: timestamp_chunk#_filename.ext
            // parse filename
            strcpy(linecpy, line);
            char *ts = strtok(linecpy, "_");
            char *chunk_num = strtok(NULL, "_");
            char *fn = strtok(NULL, "\n");
            int fn_len = strlen(fn);

            // check if file is already in array
            int new_file = 0;
            int i_edit = num_files;
            // search for file within array of filenames
            for (int i = 0; i < num_files; i++) {
                if(strncmp(fn, filenames[i], fn_len) == 0) {
                    new_file = 1;
                    i_edit = i;
                    break;
                }
            }
            // insert new filename into array of filenames
            if (new_file == 0) {
                strncpy(filenames[i_edit], fn, fn_len);
                num_files++;
            }

            // record chunk number
            int n = atoi(chunk_num);
            chunks[i_edit][n]++;
            
            line_start += strlen(line) + 1;
            line = strtok(main_buf + line_start, "\n");
            free(linecpy);
        }
        // printf("%d\n", num_files);

        // build the list of files to print to the user
        bzero(main_buf, BUFSIZE);
        for (int i = 0; i < num_files; i++) {
            int incomplete = 0;
            for (int j = 0; j < 4; j++) {
                // missing chunk
                if (chunks[i][j] == 0) {
                    incomplete = 1;
                }
            }

            strcat(main_buf, filenames[i]);
            if (incomplete == 1) {
                strcat(main_buf, "[incomplete]");
            }
            strcat(main_buf, "\n");
        }

        printf("%s", main_buf);

    } else if(strcmp("get", cmd) == 0) {
        if(argc == 2) {
            printf("Specify filename(s)\n");
            return -1;
        }

        // connect to servers
        conn_to_servers(serversockfds);

        // iterate through filenames
        bzero(main_buf, BUFSIZE);
        char tmp_buf[1024];
        for (int i = 2; i < argc; i++) {
            char fn[MAX_FILENAME_LEN];
            strcpy(fn, argv[i]);
            // create get msg with desired filename
            char *msg = malloc(4 + strlen(fn));
            sprintf(msg, "GET %s", fn);

            for(int j = 0; j < NUM_SRVS; j++) {
                if (serversockfds[j] == -1) {
                    continue;
                }
                // send msg to all servers
                int msg_sz = strlen(msg);
                send_all(serversockfds[j], &msg_sz, msg);
                // recv list of matching filenames
                bzero(tmp_buf, 1024);
                recv(serversockfds[j], tmp_buf, 1024, 0);
                strcat(main_buf, tmp_buf);
            }
            free(msg);
        
            // printf("buf: %s\n", main_buf);

            // store timestamps and chunks associated to them
            char ts_arr[MAX_NUM_FILES * NUM_SRVS][MAX_FILENAME_LEN];
            int chunks[MAX_NUM_FILES * NUM_SRVS][4];
            for(int j = 0; j < MAX_NUM_FILES * NUM_SRVS; j++) {
                for(int k = 0; k < NUM_SRVS; k++) {
                    chunks[j][k] = 0;
                }
            }
            int offsets[MAX_NUM_FILES * NUM_SRVS] = {-1};
            int num_ts = 0;
            
            // iterate through timestamped filenames
            int line_start = 0;
            char *line = strtok(main_buf, "\n");
            while (line != NULL) {
                char *linecpy = malloc(strlen(line));
                // FORMAT: timestamp_chunk#_filename.ext
                // parse filename
                strcpy(linecpy, line);
                char *ts = strtok(linecpy, "_");
                char *chunk_num = strtok(NULL, "_");
                char *f = strtok(NULL, " ");
                char *c_sz = strtok(NULL, "\n");
                // printf("%s %s %s %s\n", ts, chunk_num, f, c_sz);
                int ts_len = strlen(ts);

                // check if already in array
                int new_file = 0;
                int i_edit = num_ts;
                for (int j = 0; j < num_ts; j++) {
                    if(strncmp(ts, ts_arr[j], ts_len) == 0) {
                        new_file = 1;
                        i_edit = j;
                        break;
                    }
                }
                // insert new filename into array of filenames
                if (new_file == 0) {
                    strncpy(ts_arr[i_edit], ts, ts_len);
                    // printf("New: %s\n", ts_arr[i_edit]);
                    num_ts++;
                }

                // record chunk number
                int n = atoi(chunk_num);
                chunks[i_edit][n]++;

                // record offset
                if(atoi(chunk_num) == 0 && offsets[i_edit] < atoi(c_sz)) {
                    offsets[i_edit] = atoi(c_sz);
                }
                
                line_start += strlen(line) + 1;
                line = strtok(main_buf + line_start, "\n");
                free(linecpy);
            }

            int j;
            // iterate through timestamps, mark if incomplete
            for (j = 0; j < num_ts; j++) {
                // if missing chunks, set timestamp to -1
                for (int k = 0; k < NUM_SRVS; k++) {
                    if(chunks[j][k] == 0) {
                        strcpy(ts_arr[j], "-1");
                        break;
                    }
                }
                // printf("ts_arr[%d]: %s\n", j, ts_arr[j]);
            }
            long max_ts = -1;
            int max_i = -1;
            // find greatest timestamp (most recent)
            for (j = 0; j < num_ts; j++) {
                if(atol(ts_arr[j]) > max_ts) {
                    max_ts = atol(ts_arr[j]);
                    max_i = j;
                }
            }
            // printf("%ld, %d\n", max_ts, max_i);
            // if nothing found in arr, file is incomplete, return
            if(max_i == -1) {
                printf("%s is incomplete.\n", fn);
                break;
            }

            // open file
            int fd = open(fn, O_CREAT | O_WRONLY, 0666);
            if (fd == -1) {
                error("open");
            }

            char *ts = ts_arr[max_i]; // desired timestamp         
            for (j = 0; j < NUM_SRVS; j++) {
                // send desired timestamp
                send(serversockfds[j], ts, 32, 0);

                for (int k = 0; k < 2; k++) {
                    // recv chunk number
                    int chunk_num;
                    recv(serversockfds[j], &chunk_num, sizeof(int), 0);

                    // set correct offset to begin writing at
                    int offset = chunk_num * offsets[max_i];
                    if (lseek(fd, offset, SEEK_SET) == -1) {
                        close(fd);
                        error("lseek");
                    }

                    // recv file sz
                    int f_sz;
                    recv(serversockfds[j], &f_sz, sizeof(int), 0);
                    // recv file data
                    int n;
                    int recv_sz;
                    int bytes_recv = 0;
                    while (bytes_recv < f_sz) {
                        // receive part of the chunk
                        bzero(main_buf, BUFSIZE);
                        recv_sz = f_sz - bytes_recv < BUFSIZE ? f_sz - bytes_recv : BUFSIZE;
                        n = recv(serversockfds[j], main_buf, recv_sz, 0);
                        if (n < 0) {
                            error("Recv file failed");
                        }
                        if(n == 0) {
                            break;
                        }
                        bytes_recv += n;
                        // write chunk
                        if (write(fd, main_buf, n) == -1) {
                            close(fd);
                            error("write");
                        }
                        // printf("data: %s\n", main_buf);
                    }
                }
            }
            close(fd);
        }

    } else if(strcmp("put", cmd) == 0) {
        if(argc == 2) {
            printf("Specify filename(s)\n");
            return -1;
        }

        // connect to servers
        conn_to_servers(serversockfds);

        // iterate through filenames
        for (int i = 2; i < argc; i++) {
            char fn[50];
            strcpy(fn, argv[i]);

            // check if enough servers are available to store file
            int put_possible = 1;
            for(int j = 0; j < NUM_SRVS; j++) {
                if(serversockfds[j] == -1 && serversockfds[(j + 1) % NUM_SRVS] == -1) {
                    put_possible = 0;
                }
            }
            if(!put_possible) {
                printf("%s put failed\n", fn);
                continue;
            }

            int bucket_num = ((unsigned int) hash(fn)) % NUM_SRVS;

            // open file
            int fd = open(fn, O_RDONLY);
            if(fd < 0) {
                printf("Failed to open file\n");
                return -1;
            }

            // get length of file
            int sz = lseek(fd, 0, SEEK_END);
            // determine the size of each chunk
            int chunk_sz = sz / NUM_SRVS;
            int rem = sz % NUM_SRVS;

            // calculate chunk offsets
            int offsets[NUM_SRVS];
            int chunk_szs[NUM_SRVS];
            int offset = 0;
            for (int j = 0; j < NUM_SRVS; j++) {
                offsets[j] = offset;
                offset += chunk_sz;
                chunk_szs[j] = chunk_sz;
            }
            chunk_szs[NUM_SRVS - 1] += rem;

            // send data to appropriate servers
            int j = bucket_num;
            int chunk_i = 0;
            // get milliseconds since epoch
            struct timeval tv;
            gettimeofday(&tv, NULL);
            long long curr_time = (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
            
            while(1) {
                if(serversockfds[j] != -1) {
                    // build command msg FORMAT: timestamp_chunk#_filename.ext
                    char *msg = calloc(1, 200);
                    sprintf(msg, "PUT %lld_%d_%s %lld_%d_%s", curr_time, chunk_i, fn, curr_time, (chunk_i + 1) % NUM_SRVS, fn);
                    printf("%s\n", msg);
                    // send cmd msg
                    int len = strlen(msg);
                    send_all(serversockfds[j], &len, msg);
                    free(msg);

                    int next_chunk_i = (chunk_i + 1) % NUM_SRVS;

                    // send chunk 1
                    off_t chunk1_offset = offsets[chunk_i];
                    int chunk1_sz = chunk_szs[chunk_i];
                    // send chunk sz
                    send(serversockfds[j], &chunk1_sz, sizeof(int), 0);
                    // send chunk
                    int sent_bytes = 0;
                    int n;
                    while (sent_bytes < chunk1_sz) {
                        n = sendfile(serversockfds[j], fd, &chunk1_offset, chunk1_sz - sent_bytes);
                        if (n < 0) {
                            error("Sendfile failed");
                        }
                        sent_bytes += n;
                    }

                    // send chunk 2
                    off_t chunk2_offset = offsets[next_chunk_i];
                    int chunk2_sz = chunk_szs[next_chunk_i];
                    // send chunk_sz
                    send(serversockfds[j], &chunk2_sz, sizeof(int), 0);
                    // send chunk
                    sent_bytes = 0;
                    while (sent_bytes < chunk2_sz) {
                        n = sendfile(serversockfds[j], fd, &chunk2_offset, chunk2_sz - sent_bytes);
                        if (n < 0) {
                            error("Sendfile failed");
                        }
                        sent_bytes += n;
                    }
                }
                // iterate chunk index and conn index
                chunk_i = (chunk_i + 1) % NUM_SRVS;
                if (chunk_i == 0) {
                    break;
                }
                j = (j + 1) % NUM_SRVS;
            }
            // close file
            close(fd);
        }
    }
    else {
        printf("Invalid command. Please retry.\n");    
        return -1;    
    }
    return 0;
}

int conn_to_servers(int *serversockfds) {

    char hosts[NUM_SRVS][30];
    int ports[NUM_SRVS];

    // Open file
    FILE *fp;
    char *fn = (char*)malloc(50);
    sprintf(fn, "%s/dfc.conf", getenv("HOME"));
    fp = fopen(fn, "r");
    free(fn);
    if(fp == NULL) {
        printf("Failed to open file\n");
        return -1;
    }

    // read config file and extract hosts and ports
    for(int i = 0; i < NUM_SRVS; i++) {
        char str[50];
        if (fscanf(fp, "%*s %*s %s", str) == 0) {
            printf("Reading from file failed.\n");
            fclose(fp);
            return -1;
        }
        char *host = strtok(str, ":");
        char *port = strtok(NULL, "\n");
        strcpy(hosts[i], host);
        sscanf(port, "%d", &ports[i]);
    }
    fclose(fp);

    for (int i = 0; i < NUM_SRVS; i++) {
        // create socket
        struct sockaddr_in serveraddr;
        if ((serversockfds[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("ERROR opening socket\n");
            continue;
        }

        /*
        * build the server's Internet address
        */
        bzero((char *) &serveraddr, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        serveraddr.sin_addr.s_addr = inet_addr(hosts[i]);
        serveraddr.sin_port = htons(ports[i]);

        //Connection to the socket
        if (connect(serversockfds[i], (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0) {
            printf("Problem in connecting to the server, retrying...\n");
            serversockfds[i] = -1;
            continue;
        }
    }
    return 0;
}

void send_all(int connfd, int *sz, char *data) {
    if (send(connfd, sz, sizeof(int), 0) == -1) {
        error("Length send failed");
    } 
    int n;
    int bytes_sent = 0;
    int bytes_to_send = *sz;
    while (bytes_sent < bytes_to_send) {
        n = send(connfd, data + bytes_sent, bytes_to_send - bytes_sent, 0);
        if (n == -1) {
            error("Chunk send failed");
        }
        bytes_sent += n;
    }
    // printf("bytes_sent: %d\n", bytes_sent);
}

// hash function taken from http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash(char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}