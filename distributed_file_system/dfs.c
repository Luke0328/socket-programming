/* 
* Distributed file server
* usage: server <port>
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
#include <sys/sendfile.h>
#include <fcntl.h>

#define BUFSIZE 8192
#define LISTENQ 64 /*maximum number of client connections */

/*
* error - wrapper for perror
*/
void error(char *msg) {
    perror(msg);
    exit(1);
}

// Timeout handler
void timeout_handler(int signum) {
    printf("Child process timed out. Exiting...\n");
    exit(1);
}

// SIGINT handler
void sigint_handler(int sigsum) {
    printf("Received interrupt signal, waiting for child processes before shutting down...\n");
    while(wait(NULL) > 0);
    exit(0);
}

void list_files(char *buf, char* server_dir);

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
    char server_dir[10];

    /* 
    * check command line arguments 
    */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <server_directory> <port>\n", argv[0]);
        exit(1);
    }
    sprintf(server_dir, "%s", argv[1]);
    portno = atoi(argv[2]);

    /* 
    * socket: create the parent socket 
    */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
    * us rerun the server immediately after we kill it; 
    * otherwise we have to wait about 20 secs. 
    * Eliminates "ERROR on binding: Address already in use" error. 
    */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
    * build the server's Internet address
    */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
    * bind: associate the parent socket with a port 
    */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
        sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    // Listen on socket for incoming connection requests
    listen(sockfd, LISTENQ);

    // Set sigint handler
    signal(SIGINT, sigint_handler);
    // Ignore SIGCHLD to avoid zombie processes
    signal(SIGCHLD,SIG_IGN);

    printf("%s\n","Server running...waiting for connections.");

    for ( ; ; ) {

        clientlen = sizeof(clientaddr);
        connfd = accept(sockfd, (struct sockaddr *) &clientaddr, &clientlen);
        if(connfd == -1) {
            error("Accept error");
        }
        printf("%s\n","Received request...");
            
        if((childpid = fork()) == 0) {
            printf ("%s\n","Child created for dealing with client requests");

            // Set alarm for timeout of 10 s
            signal(SIGALRM, timeout_handler);
            alarm(10);
            // Ignore SIGINT for children (sent via process group), timeout will handle any that take too long
            signal(SIGINT, SIG_IGN);

            close(sockfd); 

            // sleep(5); // For testing
            while(1) {
                // recv command length
                int cmd_len;
                if (recv(connfd, &cmd_len, sizeof(int), 0) <= 0) {
                    error("Recv failed");
                }
                // recv command from client
                char *cmdbuf = malloc(cmd_len + 1);
                if (recv(connfd, cmdbuf, cmd_len, 0) <= 0) {
                    error("Recv failed");
                }
                cmdbuf[cmd_len] = '\0';

                // FORMAT: cmd chunk_name_1 chunk_name_2
                char *cmd = strtok(cmdbuf, " ");
                char *fn1_tok = strtok(NULL, " ");
                char *fn2_tok = strtok(NULL, " ");
                printf("%s %s %s\n", cmd, fn1_tok, fn2_tok);

                // build the correct filenames for writing
                char fns[2][64];
                if (fn1_tok != NULL) {
                    sprintf(fns[0], "%s/%s", server_dir, fn1_tok);
                }
                if (fn2_tok != NULL) {
                    sprintf(fns[1], "%s/%s", server_dir, fn2_tok);
                }

                if (strncmp(cmd, "LIST", 4) == 0) {
                    printf("list recvd\n");

                    char *buf = calloc(1024, 1);
                    list_files(buf, server_dir);

                    send(connfd, buf, 1024, 0);
                    free(buf);

                } else if (strncmp(cmd, "GET", 3) == 0) {

                    char buf[1024];
                    // get list of all filenames
                    list_files(buf, server_dir);

                    char send_buf[1024];
                    bzero(send_buf, 1024);
                    char *line = strtok(buf, "\n");
                    while (line != NULL) {
                        // check if filename matches filename from GET
                        if(strcmp(line + (strlen(line) - strlen(fn1_tok)), fn1_tok) == 0 && line[strlen(line) - strlen(fn1_tok) - 1] == '_') {
                            // append file sizes
                            char fn[128];
                            sprintf(fn, "%s/%s", server_dir, line);
                            int fd = open(fn, O_RDONLY);
                            if(fd < 0) {
                                error("Failed to open file");
                            }
                            int sz = lseek(fd, 0, SEEK_END);
                            close(fd);
                            sprintf(send_buf + strlen(send_buf), "%s %d\n", line, sz);
                        }
                        line = strtok(NULL, "\n");
                    }
                    // send list with matching filenames
                    send(connfd, send_buf, 1024, 0);

                    // recv desired timestamp
                    char ts[32];
                    recv(connfd, ts, 32, 0);
                    // search for desired timestamp
                    line = strtok(send_buf, "\n");
                    while (line != NULL) {
                        char *i = strstr(line, "_");
                        int chunk_num = atoi(i + 1);
                        // search for matching timestamp
                        if (strncmp(line, ts, strlen(line) - strlen(i)) == 0) {
                            char *ind = strstr(line, " ");
                            strcpy(ind, "\0");

                            char fn[128];
                            sprintf(fn, "%s/%s", server_dir, line);

                            int fd = open(fn, O_RDONLY);
                            if(fd < 0) {
                                error("Failed to open file");
                            }
                            int sz = lseek(fd, 0, SEEK_END);
                            //send chunk number
                            send(connfd, &chunk_num, sizeof(int), 0);
                            // send size of file
                            send(connfd, &sz, sizeof(int), 0);
                            // send file
                            int n;
                            off_t offset = 0;
                            while (offset < sz) {
                                n = sendfile(connfd, fd, &offset, sz);
                                if (n < 0) {
                                    error("Sendfile failed");
                                }
                            }
                            close(fd);
                        }
                        line = strtok(NULL, "\n");
                    }

                } else if (strncmp(cmd, "PUT", 3) == 0) {
                    for(int i = 0; i < 2; i++) {
                        // open file 
                        int fd = open(fns[i], O_CREAT | O_WRONLY, 0666);
                        if(fd == -1) {
                            error("Failed to open file");
                        }
                        // recv file len
                        int f_sz;
                        if (recv(connfd, &f_sz, sizeof(int), 0) <= 0) {
                            error("Recv file 1 len failed");
                        }

                        // recv file data
                        int n;
                        int recv_sz;
                        int bytes_recv = 0;
                        while (bytes_recv < f_sz) {
                            // receive part of the chunk
                            bzero(main_buf, BUFSIZE);
                            recv_sz = f_sz - bytes_recv < BUFSIZE ? f_sz - bytes_recv : BUFSIZE;
                            n = recv(connfd, main_buf, recv_sz, 0);
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
                        close(fd);
                    }
                }
                free(cmdbuf);
                exit(0);
            }
        }
        close(connfd);
    }
    close(sockfd);
    return 0;
}

void list_files(char *buf, char* server_dir) {
    // get and store file names of local files
    DIR *dir;
    dir = opendir(server_dir);
    if (dir == NULL) {
        error("opendir failed");
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            sprintf(buf + strlen(buf),"%s\n", entry->d_name);
        }
    }
    closedir(dir);
}