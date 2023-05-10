/* 
* Basic TCP Web Server
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

// Declare function prototypes
int parse_request(char* buf, char** req_method, char* req_uri, char** req_ver, 
char* status_code, char** file_ext);
int build_response(int connfd, char* req_uri, char* req_ver, char* status_code, char* file_ext);
int build_err_response(int connfd, char* req_ver, char* status_code);
int get_cont_type(char* ext, char* buf);

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int connfd; /* connection*/
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp; /* client host info */
    char buf[BUFSIZE]; /* message buf */
    char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    pid_t childpid;

    /* 
    * check command line arguments 
    */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

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
        connfd = accept (sockfd, (struct sockaddr *) &clientaddr, &clientlen);
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

            bzero(buf, BUFSIZE);

            int n;

            while (1)  {
                n = recv(connfd, buf, BUFSIZE, 0);

                if(n == 0) {
                    printf("Conn closed by client\n");
                    exit(0);
                }
                else if (n < 0) {
                    perror("Read error");
                    exit(1);
                }
                int tot = n;
                
                while(strncmp(buf + tot - 4, "\r\n\r\n", 4) != 0) {

                    n = recv(connfd, buf + tot, BUFSIZE - tot, 0);

                    if(n == 0) {
                        exit(0);
                    }
                    else if (n < 0) {
                        perror("Read error");
                        exit(1);
                    }
                    tot += n;
                }

                printf("%s %s\n","String received from the client:", buf);

                char* req_method;
                char req_uri[128];
                char* req_ver;
                char status_code[32];
                char* file_ext;
                char* res;

                if(parse_request(buf, &req_method, req_uri, &req_ver, status_code, &file_ext) == -1) {
                    build_err_response(connfd, req_ver, status_code);
                }
                else {
                    build_response(connfd, req_uri, req_ver, status_code, file_ext);
                }

                bzero(buf, BUFSIZE);
                printf("Child exiting.\n");

                exit(0);
            }
        } 
        close(connfd);
    }
    close(sockfd);
    return 0;
}

int parse_request(char* buf, char** req_method, char* req_uri, char** req_ver, 
char* status_code, char** file_ext) {
    // Parse request method, URI, and version from request
    *req_method = strtok(buf, " ");
    char* req_uri_token = strtok(NULL, " ");
    *req_ver = strtok(NULL, "\r\n");

    // If any fields are NULL, request misformed
    if(*req_method == NULL || req_uri_token == NULL || *req_ver == NULL) {
        *req_ver = "HTTP/1.0";
        strcpy(status_code,"400 Bad Request");
        return -1;
    }

    // Handle when method other than GET is called
    if(strncmp(*req_method, "GET", 3) != 0) {
        strcpy(status_code,"405 Method Not Allowed");
        printf("%s\n", status_code);
        return -1;
    }

    // Handle any unsupported HTTP versions
    if(strncmp(*req_ver, "HTTP/1.1", 8) != 0 && strncmp(*req_ver, "HTTP/1.0", 8) != 0) {
        strcpy(status_code,"505 HTTP Version Not Supported");
        printf("%s\n", status_code);
        return -1;
    }

    strcat(req_uri, "www/"); // append www to search in subdirectory
    if(req_uri_token[0] == '/') {
        strcat(req_uri, req_uri_token+1);
    }
    else {
        strcat(req_uri, req_uri_token);
    }

    printf("%s\n", *req_method);
    printf("%s\n", req_uri);
    printf("%s\n", *req_ver);

    // Get the file type, if directory passed - recognize that
    int dir_passed = 0;
    char* front = strtok(req_uri_token, ".");
    *file_ext = strtok(NULL, "\n");

    if(*file_ext == NULL) {
        dir_passed = 1;
        printf("Directory for URI\n");
    }

    // Check if file exists
    if(dir_passed == 1) {
        strcat(req_uri, "index.html");
    }
    if(access(req_uri, F_OK) == -1) {
        req_uri[strlen(req_uri) - 1] = '\0'; // try with index.htm now
        printf("%s\n", req_uri);
    } 
    if(access(req_uri, F_OK) == -1) { // if still not found, send 404 status code
        printf("%s\n", "File does not exist.");
        strcpy(status_code,"404 Not Found");
        printf("%s\n", status_code);
        return -1;
    }

    // Check if file is accessible
    if(access(req_uri, R_OK) == -1) {
        printf("%s\n", "File is not accessible.");
        strcpy(status_code,"403 Forbidden");
        printf("%s\n", status_code);
        return -1;
    } 

    // Set the error code to valid
    strcpy(status_code, "200 OK");
    return 0;
}

int build_response(int connfd, char* req_uri, char* req_ver, char* status_code, char* file_ext) {
    // Create buffer for response
    char first_line[128];
    char cont_type[64];
    char cont_len[64];

    // Build first line (version status code)
    sprintf(first_line, "%s %s", req_ver, status_code);

    // Determine content type
    if (get_cont_type(file_ext, cont_type) == -1) {
        printf("File type not valid.\n");
    }

    // Open file
    FILE *fp;
    fp = fopen(req_uri, "r");
    if(fp == NULL) {
        printf("Failed to open file\n");
        return -1;
    }

    // Determine content length
    fseek(fp, 0L, SEEK_END);
    int file_sz = ftell(fp);
    rewind(fp);
    sprintf(cont_len, "%d", file_sz); // put size into cont_len as string

    // Build header and body for response
    char header[512];
    sprintf(header, "%s\r\nContent-Type: %s\r\nContent-Length: %s\r\n\r\n", first_line, cont_type, cont_len);

    // Send header out first
    int tot_n = 0;
    int bytes_left = strlen(header);
    int n;

    while(tot_n < strlen(header)) {
        n = send(connfd, header + tot_n, bytes_left, 0);
        tot_n += n;
        bytes_left -= n;
    }

    // Send file data
    char res[BUFSIZE];

    int bytes_sent_from_file = 0;
    int bytes_left_from_file = file_sz;

    // Read and send entire file
    while(bytes_sent_from_file < file_sz) {

        bzero(res, BUFSIZE);
        int bytes_read;
        int bytes_to_read = BUFSIZE;

        // if there is a partial packet, use the size of the partial packet
        if(file_sz - bytes_sent_from_file < BUFSIZE) {
            bytes_to_read = file_sz - bytes_sent_from_file;
        }

        // Get contents of file
        bytes_read = fread(res, 1, bytes_to_read, fp);
        if (bytes_read == -1) {
            printf("Reading from file failed.\n");
            fclose(fp);
            return -1;
        }

        // Make sure entire packet is sent
        int tot_n = 0;
        int bytes_left = bytes_read;
        int n;

        while(tot_n < bytes_read) {
            n = send(connfd, res + tot_n, bytes_left, 0);
            tot_n += n;
            bytes_left -= n;
        }

        bytes_sent_from_file += tot_n;
    }

    fclose(fp);

    return 0;
}

int build_err_response(int connfd, char* req_ver, char* status_code) {
    // Build error response
    char res[BUFSIZE];
    sprintf(res, "%s %s\r\nContent-Type:\r\nContent-Length:0\r\n\r\n", req_ver, status_code);
    int res_sz = strlen(res);

    int bytes_sent = 0;
    int n;

    // Make sure entire response is sent
    while(bytes_sent < res_sz) {
        n = send(connfd, res, res_sz, 0);
        bytes_sent += n;
    }

    return 0;
}

// Get content type from file extention
int get_cont_type(char* ext, char* buf) {
    if(ext == NULL) { // if directory was passed
        strcpy(buf, "text/html");
    } else if(!strcmp(ext, "html")){
        strcpy(buf, "text/html");
    } else if (!strcmp(ext, "txt")){
        strcpy(buf, "text/plain");
    } else if (!strcmp(ext, "png")){
        strcpy(buf, "image/png");
    } else if (!strcmp(ext, "gif")){
        strcpy(buf, "image/gif");
    } else if (!strcmp(ext, "jpg")){
        strcpy(buf, "image/jpg");
    } else if(!strcmp(ext, "ico")) {
        strcpy(buf, "image/x-icon");
    } else if (!strcmp(ext, "css")){
        strcpy(buf, "text/css");
    } else if (!strcmp(ext, "js")){
        strcpy(buf, "application/javascript");
    } else {
        return -1;
    }
    return 0;
}