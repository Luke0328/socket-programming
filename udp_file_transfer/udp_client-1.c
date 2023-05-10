/* 
 * udpclient.c - A simple UDP client
 * usage: udpclient <host> <port>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define BUFSIZE 1024

int get_func(char *buf, int n, int *sockfd, struct  
  sockaddr_in *serveraddr, int *serverlen);
int put_func(char *buf, int n, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen);
int delete_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen);
int ls_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen);
int exit_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen);
int send_msg(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen);


/* 
 * error - wrapper for perror
 */
void error(char *msg) {
    perror(msg);
    exit(0);
}

int main(int argc, char **argv) {
    int sockfd, portno, n;
    int serverlen;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char *hostname;
    char buf[BUFSIZE];

    /* check command line arguments */
    if (argc != 3) {
       fprintf(stderr,"usage: %s <hostname> <port>\n", argv[0]);
       exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* gethostbyname: get the server's DNS entry */
    server = gethostbyname(hostname);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host as %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
	  (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(portno);

    const char c[2] = " ";

    while(1) {
      /* get a message from the user */
      bzero(buf, BUFSIZE);
      printf("Please enter msg:\n");
      fgets(buf, BUFSIZE, stdin);

      serverlen = sizeof(serveraddr);
      n = strlen(buf);

      if(feof(stdin)) {
        exit(0);
      }
      
      if(strncmp(buf, "get", 3) == 0) {
        printf("true");
        if(send_msg((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Failed to send message\n");
        }

        if (get_func((char*)&buf, n - 4 - 1, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Get Failed\n");
        }
      }
      else if(strncmp(buf, "put ", 4) == 0) {
        if(send_msg((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Failed to send message\n");
        }
        if (put_func((char*)&buf, n - 4 - 1, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Put Failed\n");
        }
      }
      else if(strncmp(buf, "delete ", 7) == 0) {
        if(send_msg((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Failed to send message\n");
        }
        if (delete_func((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          
        }
      }
      else if(strncmp(buf, "ls", 2) == 0) {
        if(send_msg((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Failed to send message\n");
        }

        if (ls_func((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("ls Failed\n");
        }
      }
      else if(strncmp(buf, "exit", 4) == 0) {
        if(send_msg((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Failed to send message\n");
        }

        if (exit_func((char*)&buf, &sockfd, &serveraddr, &serverlen) == -1) {
          printf("Exit Failed\n");
        }
        else {
          exit(0);
        }
      }
      else {
        printf("Invalid Input\n");
      }

      /* print the server's reply */
      // n = recvfrom(sockfd, buf, strlen(buf), 0, (struct sockaddr *) &serveraddr, &serverlen);
      // if (n < 0) 
      //   error("ERROR in recvfrom");
      // printf("Echo from server: %s", buf);
      
    }
  return 0;
}

int get_func(char *buf, int n, int *sockfd, struct  
  sockaddr_in *serveraddr, int *serverlen) {

  // get filename
  char fn[100];
  buf += 4;
  memcpy(fn, buf, n);
  fn[n] = '\0';

  // move pointer back to beginning
  buf -= 4;

  // recv either failed msg or file length
  bzero(buf, BUFSIZE);
  n = recvfrom(*sockfd, buf, BUFSIZE, 0,
    (struct sockaddr *) serveraddr, &(*serverlen));
  if (n < 0) {
    error("ERROR in recvfrom");
    return -1;
  }  

  // if fail message received from client, return -1 without writing
  if(strncmp(buf, "Get Failed", 10) == 0) {
    return -1;
  }
  char *remaining;
  long len = strtol(buf, &remaining, 10);
  printf("%ld\n", len);

  // open file using filename
  FILE *fp;
  fp = fopen(fn, "w");

  if(fp == NULL) {
    printf("File could not be opened\n");
    return -1;
  }

  long total = 0;

  while(total < len) {
    // recv data from client
    bzero(buf, BUFSIZE);
    n = 0;
    n = recvfrom(*sockfd, buf, BUFSIZE, 0,
      (struct sockaddr *) serveraddr, &(*serverlen));
    if (n < 0) {
      error("ERROR in recvfrom");
      return -1;
    }
    printf("Bytes Recv: %d\n", n);

    total += n;

    // write to the file
    int bytes_written = fwrite(buf, 1, n, fp);
    if(bytes_written == -1) {
      printf("Writing to file failed\n");
      return -1;
    }

    printf("Bytes Written: %d\n", bytes_written);
    printf("%ld\n", total);

    // send ack to server
    bzero(buf, BUFSIZE);
    sprintf(buf, "%d", n);
    n = sendto(*sockfd, buf, strlen(buf), 0, 
      (struct sockaddr *) &(*serveraddr), *serverlen);
    if (n < 0) {
      error("ERROR in sendto");
      return -1;
    }
  }
    
  fclose(fp);
  return 1;
}

int put_func(char *buf, int n, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen) {
  // get filename
  char fn[100];
  buf += 4;
  memcpy(fn, buf, n);
  fn[n] = '\0';

  // move pointer back to beginning
  buf -= 4;

  // open file
  FILE *fp;
  fp = fopen(fn, "r");

  if(fp == NULL) {
    printf("File could not be opened\n");
    return -1;
  }

  // get size of file
  long len;
  fseek(fp, 0L, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0L, SEEK_SET);

  // send length of file to client
  bzero(buf, BUFSIZE);
  sprintf(buf, "%ld", len);
  n = sendto(*sockfd, buf, strlen(buf), 0, 
    (struct sockaddr *) serveraddr, *serverlen);
  if (n < 0) {
    error("ERROR in sendto");
    return -1;
  }

  printf("File size: %ld\n", len);

  long total = 0;

  while(total < len) {
    // read from file into buffer
    bzero(buf, BUFSIZE);
    int bytes_read;
    int bytes_to_read = BUFSIZE;

    // if there is a partial packet, use the size of the partial packet
    if(len - total < BUFSIZE) {
      bytes_to_read = len - total;
    }

    // read from file
    bytes_read = fread(buf, 1, bytes_to_read, fp);
    if (bytes_read == -1) {
      printf("Reading from file failed\n");
      fclose(fp);
      return -1;
    }

    /* 
    * sendto: send file data to the client 
    */
    n = sendto(*sockfd, buf, bytes_read, 0, 
          (struct sockaddr *) serveraddr, *serverlen);
    if (n < 0) {
      error("ERROR in sendto");
      fclose(fp);
      return -1;
    }

    // update total bytes sent
    total += n;
    printf("Bytes sent: %d\n", n);

    //if bytes sent is less than bytes read, seek back and reread them
    if(n < bytes_read) {
      fseek(fp, total, SEEK_SET);
    }

    // recv ack from client
    bzero(buf, BUFSIZE);
    n = recvfrom(*sockfd, buf, BUFSIZE, 0,
      (struct sockaddr *) (&(*serveraddr)), &(*serverlen));
    if (n < 0) {
      error("ERROR in recvfrom");
      return -1;
    }

  }
  printf("%ld\n", total);
    
  fclose(fp);
  return 1;
}

int delete_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen) {
  int n = 0;
  bzero(buf, BUFSIZE);
  n = recvfrom(*sockfd, buf, BUFSIZE, 0, (struct sockaddr *) (&(*serveraddr)), &(*serverlen));
  if (n < 0) 
    error("ERROR in recvfrom");

  // if recv success msg -> print success msg
  if(strncmp(buf, "Delete successful", 17) == 0) {
    printf("%s\n", buf);
    return 0;
  }
  // else print failed msg
  else {
    printf("%s\n", buf);
    return -1;
  }
}

int ls_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen) {
  int n = 0;
  bzero(buf, BUFSIZE);
  // recv and print
  n = recvfrom(*sockfd, buf, BUFSIZE, 0, (struct sockaddr *) (&(*serveraddr)), &(*serverlen));
  if (n < 0) 
    error("ERROR in recvfrom");
  else {
    printf("%s", buf);
  }

  // printf("%d\n", n);
}

int exit_func(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen) {
  int n = 0;
  bzero(buf, BUFSIZE);
  // recv data from server
  n = recvfrom(*sockfd, buf, BUFSIZE, 0, (struct sockaddr *) (&(*serveraddr)), &(*serverlen));
  if (n < 0) 
    error("ERROR in recvfrom");

  // if recvd goodbye from server -> print goodbye and exit
  if(strncmp(buf, "Goodbye!", 8) == 0) {
    printf("%s\n", buf);
    return 0;
  }
  else {
    return -1;
  }
}

// wrapper function for sending datagram to server
int send_msg(char *buf, int *sockfd, struct sockaddr_in *serveraddr, int *serverlen) {
    /* send the message to the server */
    int n;
    n = sendto(*sockfd, buf, strlen(buf), 0, (struct sockaddr *) (&(*serveraddr)), *serverlen);
    if (n < 0) 
      error("ERROR in sendto");
}