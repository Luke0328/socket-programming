/* 
* Basic TCP Caching Proxy
* usage: server <port> <cache expiration time>
* client <-> proxy <-> server/host
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h> 
#include <signal.h>

#define BUFSIZE 8192
#define LISTENQ 1024 /*maximum number of client connections */

/*
* error - wrapper for error
*/
void error(char *msg) {
    error(msg);
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

int parse_request(char* b, char** req_method, char* req_url, char** req_ver, 
char* status_code, char* host_name, struct hostent **host, char* host_port, int *is_dynamic);
int in_blocklist(char *host_name);
int build_err_response(int connfd, char* req_ver, char* status_code);
int sendall(int connfd, char *b, int len);
int recv_header(int connfd, char *buf);
int check_cache(char *res, int connfd, char *fn, int timeout);
unsigned long hash_func( char *str);

int main(int argc, char **argv) {
    int sockfd; /* socket */
    int connfd; /* connection*/
    int portno; /* port to listen on */
    int cache_timeout;
    int clientlen; /* byte size of client's address */
    struct sockaddr_in proxyaddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    struct hostent *hostp; /* client host info */
    char buf[BUFSIZE]; /* message buf */
    char *hostaddrp; /* dotted decimal host addr string */
    int optval; /* flag value for setsockopt */
    pid_t childpid;

    /* 
    * check command line arguments 
    */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> <cache_timeout>\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);
    cache_timeout = atoi(argv[2]);

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
    bzero((char *) &proxyaddr, sizeof(proxyaddr));
    proxyaddr.sin_family = AF_INET;
    proxyaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    proxyaddr.sin_port = htons((unsigned short)portno);

    /* 
    * bind: associate the parent socket with a port 
    */
    if (bind(sockfd, (struct sockaddr *) &proxyaddr, 
        sizeof(proxyaddr)) < 0) 
        error("ERROR on binding");

    // Listen on socket for incoming connection requests
    listen(sockfd, LISTENQ);

    // Set sigint handler
    signal(SIGINT, sigint_handler);
    // Ignore SIGCHLD to avoid zombie processes
    signal(SIGCHLD,SIG_IGN);

    printf("%s\n","Proxy running...waiting for connections.");

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

            while (1)  {
                // receive the full header from client, since we are only handling GETs we can ignore the HTTP body
                recv_header(connfd, buf);

                printf("%s %s\n","String received from the client:\n", buf);

                char* req_method;
                char req_url[288];
                char* req_ver;
                char status_code[32];

                char host_name[128];
                struct hostent *host;
                char host_port[32];
                int is_dynamic = 0;

                // validate http request
                if(parse_request(buf, &req_method, req_url, &req_ver, status_code, host_name, &host, host_port, &is_dynamic) == -1) {
                    build_err_response(connfd, req_ver, status_code);
                    exit(0);
                }

                // check if in cache, if in cache, send from cache
                if(check_cache(buf, connfd, req_url, cache_timeout) == 0) { 
                    exit(0);
                }

                // open file for writing to
                FILE *fp;
                char hash_fn[128];
                if(is_dynamic == 0) {
                    unsigned long ul = hash_func(req_url);
                    sprintf(hash_fn, "cache/%ld", ul);
                    fp = fopen(hash_fn, "w");
                    if(fp == NULL) {
                        error("File could not be opened for writing\n");
                    }
                }

                // connect to server:
                // create socket
                int serversockfd;
                struct sockaddr_in serveraddr;
                if ((serversockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
                    error("ERROR opening socket");
                    exit(2);
                }

                /*
                * build the server's Internet address
                */
                bzero((char *) &serveraddr, sizeof(serveraddr));
                serveraddr.sin_family = AF_INET;
                serveraddr.sin_addr.s_addr = ((struct in_addr*) (host->h_addr_list[0]))->s_addr;
                serveraddr.sin_port = htons((unsigned short)atoi(host_port));

                //Connection to the socket
                if (connect(serversockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr))<0) {
                    error("Problem in connecting to the server");
                    exit(3);
                }

                // pass req to server
                sendall(serversockfd, buf, strlen(buf));

                // recv request from server
                int n;
                while(1) {
                    bzero(buf, BUFSIZE);
                    n = recv(serversockfd, buf, BUFSIZE, 0);
                    // printf("%s\n", buf);
                    // printf("%d\n", n);
                    // printf("%ld\n", strlen(buf));
                    if(n == 0) {
                        printf("Connection closed by server.\n");
                        break;
                    } else if (n < 0) {
                        error("Recv error");
                    }

                    // send partial res body to client
                    sendall(connfd, buf, n);

                    // write to the file if request was not for dynamic content
                    if(is_dynamic == 0) {
                        if(fwrite(buf, 1, n, fp) == -1) {
                            error("Writing to file failed");
                        }
                    }
                }
                if(is_dynamic == 0) {
                    fclose(fp);
                }
                close(serversockfd);
                printf("Child exiting.\n");
                exit(0);
            }
        } 
        close (connfd);
    }
    close(sockfd);
    return 0;
}

int parse_request(char* b, char** req_method, char* req_url, char** req_ver, 
char* status_code, char* host_name, struct hostent **host, char* host_port, int *is_dynamic) {
    // Copy to preserve original
    char buf[BUFSIZE];
    bzero(buf, BUFSIZE);
    strcpy(buf, b);
    // printf("buf: %s\n", buf);

    // Parse first line
    char* firstline = strtok(buf, "\r\n");
    char* headers = buf + strlen(firstline) + 2;
    // printf("firstline: %s\n", firstline);
    // printf("headers: %s\n", headers);

    // Parse request method, URL, and version from request
    *req_method = strtok(firstline, " ");
    char* req_url_token = strtok(NULL, " ");
    *req_ver = strtok(NULL, "\r\n");

    // If any fields are NULL, or method other than GET request misformed
    if(*req_method == NULL || req_url_token == NULL || *req_ver == NULL || (strncmp(*req_method, "GET", 3) != 0)) {
        *req_ver = "HTTP/1.0";
        strcpy(status_code,"400 Bad Request");
        return -1;
    }

    // Handle any unsupported HTTP versions
    if(strncmp(*req_ver, "HTTP/1.1", 8) != 0 && strncmp(*req_ver, "HTTP/1.0", 8) != 0) {
        strcpy(status_code,"505 HTTP Version Not Supported");
        printf("%s\n", status_code);
        return -1;
    }

    printf("%s\n", *req_method);
    printf("%s\n", req_url_token);
    printf("%s\n", *req_ver);

    // Check if request contains '?' for dynamic content
    char *q_mark = strstr(req_url_token, "?");
    if(q_mark != NULL) {
        *is_dynamic = 1;
        printf("Dynamic content will not be cached.\n");
    }

    // Parse host and port number from headers
    char* host_header = strstr(headers, "Host: ");
    const char *delim = "\r\n";
    char *d = strstr(host_header, delim);
    d[0] = '\0';
    // printf("host_header: %s\n", host_header);

    char* i = host_header + strlen("Host: ");
    // printf("i: %s\n", i);


    char *colon = strstr(i, ":");
    char* host_tok = strtok(i, ":");
    char *port_tok = NULL;
    if(colon != NULL) {
        port_tok = strtok(NULL, "\r\n");
    }

    strcpy(host_name, host_tok);
    if(port_tok == NULL) {
        strcpy(host_port, "80"); // set to 80 if not specified
    } else {
        strcpy(host_port, port_tok);
    }

    strcat(req_url, host_name); // concat host name for identification
    if(strncmp(req_url_token, "/", 1) != 0) {
        strcat(req_url, req_url_token);
    }

    printf("host_name: %s\n", host_name);
    printf("host_port: %s\n", host_port);

    // check if valid host
    struct hostent *valid_host = gethostbyname(host_name);
    if(valid_host == NULL) {
        strcpy(status_code,"404 Not Found");
        printf("Host not valid\n");
        return -1;
    }
    // printf("%d\n", valid_host->h_addrtype);
    // printf("%s\n", valid_host->h_name);
    // printf("%d\n", ((struct in_addr*) (valid_host->h_addr_list[0]))->s_addr);
    *host = valid_host;

    // check if on blocklist
    if (in_blocklist(host_name) == 1) {
        printf("Host is blocked\n");
        strcpy(status_code, "403 Forbidden");
        return -1;
    }

    // Set the error code to valid
    strcpy(status_code, "200 OK");
    return 0;
}

int in_blocklist(char *host_name) {
    char cmd[50];
    FILE *fp;

    sprintf(cmd, "grep %s blocklist", host_name);
    fp = popen(cmd, "r");

    char out[64];
    bzero(out, 64);
    fgets(out, sizeof(out), fp);
    // printf("%s\n", out);

    pclose(fp);

    // host is in blocklist
    if(strlen(out) != 0) {
        return 1;
    }
    return 0;
}

int build_err_response(int connfd, char* req_ver, char* status_code) {
    // Build error response
    char err_res[1024];
    bzero(err_res, 1024);

    sprintf(err_res, "%s %s\r\nContent-Type:\r\nContent-Length: 0\r\n\r\n", req_ver, status_code);

    // send error response
    sendall(connfd, err_res, strlen(err_res));  
    return 0;
}

int sendall(int connfd, char *b, int len) {
    int total = 0;        // how many bytes we've sent
    int bytesleft = len; // how many we have left to send
    int n;

    while(total < len) {
        n = send(connfd, b+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }
    if(n == -1) {
        printf("Sendall failed\n");
        return -1;
    }

    return n; // return bytes sent
}

// recv until the end of the get request, indicated with \r\n\r\n
int recv_header(int connfd, char *buf) {
    int n;
    n = recv(connfd, buf, BUFSIZE, 0);

    if(n == 0) {
        printf("Conn closed by client\n");
        return 1;
    } else if (n < 0) {
        error("Recv error");
    }
    int tot = n;
    
    while(strncmp(buf + tot - 4, "\r\n\r\n", 4) != 0) {

        n = recv(connfd, buf + tot, BUFSIZE - tot, 0);

        if(n == 0) {
            printf("Conn closed by client\n");
            return 1;
        } else if (n < 0) {
            error("Recv error");
        }
        tot += n;
    }

    return 1;
}

int check_cache(char *res, int connfd, char *fn, int timeout) {

    // create ./cache if does not exist
    struct stat fstat = {0};
    if (stat("./cache", &fstat) == -1) {
        printf("Creating ./cache directory.\n");
        mkdir("./cache", 0700);
    }

    // get hash
    char hash[128];
    unsigned long ul = hash_func(fn);
    sprintf(hash, "cache/%ld", ul);
    // printf("%s", hash);

    // return if file not in cache
    if(access(hash, F_OK) == -1) {
        printf("Not in cache.\n");
        return -1;
    } 

    // in cache
    printf("In cache.\n");

    // check if expired
    if(stat(hash, &fstat) == -1) {
        printf("Error calling stat.\n");
        return- 1;
    }
    time_t time_modified = fstat.st_mtime;
    // printf("%ld\n", time(NULL) - time_modified);
    if(time(NULL) - time_modified > timeout) {
        printf("In cache but expired.\n");
        return -1;
    }

    printf("Sending from cache...\n");

    // send from file:

    // Open file
    FILE *fp;
    fp = fopen(hash, "r");
    if(fp == NULL) {
        printf("Failed to open file\n");
        return -1;
    }

    // Determine content length
    fseek(fp, 0L, SEEK_END);
    int file_sz = ftell(fp);
    rewind(fp);

    // Send file data
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
        bytes_sent_from_file += sendall(connfd, res, bytes_read);
    }

    fclose(fp);
    return 0;
}

// hash function taken from http://www.cse.yorku.ca/~oz/hash.html
unsigned long hash_func( char *str)
{
    unsigned long hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}