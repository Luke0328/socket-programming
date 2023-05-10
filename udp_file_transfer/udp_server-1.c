/* 
 * udpserver.c - A simple UDP echo server 
 * usage: udpserver <port>
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dirent.h> 

#define BUFSIZE 1024

int get_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen);
int put_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen);
int del_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen);
int ls_func(char *buf, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen);
int exit_func(int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen);

/*
 * error - wrapper for perror
 */
void error(char *msg) {
  perror(msg);
  exit(1);
}

int main(int argc, char **argv) {
  int sockfd; /* socket */
  int portno; /* port to listen on */
  int clientlen; /* byte size of client's address */
  struct sockaddr_in serveraddr; /* server's addr */
  struct sockaddr_in clientaddr; /* client addr */
  struct hostent *hostp; /* client host info */
  char buf[BUFSIZE]; /* message buf */
  char *hostaddrp; /* dotted decimal host addr string */
  int optval; /* flag value for setsockopt */
  int n; /* message byte size */

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
  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
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

  /* 
   * main loop: wait for a datagram, then echo it
   */
  clientlen = sizeof(clientaddr);
  while (1) {

    /*
     * recvfrom: receive a UDP datagram from a client
     */
    bzero(buf, BUFSIZE);
    n = recvfrom(sockfd, buf, BUFSIZE, 0,
		 (struct sockaddr *) &clientaddr, &clientlen);
    if (n < 0)
      error("ERROR in recvfrom");

    // Handle keywords
    if(strncmp(buf, "get ", 4) == 0) {
      if (get_func(&buf[4], n - 4 - 1, &sockfd, &clientaddr, &clientlen) == -1) {
        perror("\nError:");
        printf("get failed\n");
      }
    }
    else if(strncmp(buf, "put ", 4) == 0) {
      if (put_func(&buf[4], n - 4 - 1, &sockfd, &clientaddr, &clientlen) == -1) {
        perror("\nError:");
        printf("put failed\n");
      }
    }
    else if(strncmp(buf, "delete ", 7) == 0) {
      if (del_func(&buf[7], n - 7 - 1, &sockfd, &clientaddr, &clientlen) == -1) {
        perror("\nError:");
        printf("delete failed\n");
      }
    }
    else if(strncmp(buf, "ls", 2) == 0) {
      if (ls_func((char*)&buf, &sockfd, &clientaddr, &clientlen) == -1) {
        printf("ls failed\n");
      }
    }
    else if(strncmp(buf, "exit", 4) == 0) {
      if (exit_func(&sockfd, &clientaddr, &clientlen) == -1) {
        printf("exit failed\n");
      }
    }



    /* 
     * gethostbyaddr: determine who sent the datagram
     */
    hostp = gethostbyaddr((const char *)&clientaddr.sin_addr.s_addr, 
			  sizeof(clientaddr.sin_addr.s_addr), AF_INET);
    if (hostp == NULL)
      error("ERROR on gethostbyaddr");
    hostaddrp = inet_ntoa(clientaddr.sin_addr);
    if (hostaddrp == NULL)
      error("ERROR on inet_ntoa\n");
    printf("server received datagram from %s (%s)\n", 
	   hostp->h_name, hostaddrp);
    // printf("server received %ld/%d bytes: %s\n", strlen(buf), n, buf);
    
    /* 
     * sendto: echo the input back to the client 
     */
    // n = sendto(sockfd, buf, strlen(buf), 0, 
	  //      (struct sockaddr *) &clientaddr, clientlen);
    // if (n < 0) 
    //   error("ERROR in sendto");
  }
}

int get_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen) {
  // get filename
  char fn[100];
  memcpy(fn, buf, n);
  fn[n] = '\0';

  // move pointer back to beginning
  buf -= 4;

  FILE *fp;
  fp = fopen(fn, "r");

  // try to open file - if failed, send failed msg
  if(fp == NULL) {
    printf("File could not be opened\n");
    char *msg = "Get Failed";
    n = sendto(*sockfd, msg, strlen(msg), 0, 
        (struct sockaddr *) &(*clientaddr), *clientlen);
    if (n < 0) {
      error("ERROR in sendto");
    }
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
    (struct sockaddr *) clientaddr, *clientlen);
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
          (struct sockaddr *) clientaddr, *clientlen);
    if (n < 0) {
      error("ERROR in sendto");
      fclose(fp);
      return -1;
    }

    total += n;
    printf("Bytes sent: %d\n", n);

    //if bytes sent is less than bytes read, seek back and reread them
    if(n < bytes_read) {
      fseek(fp, total, SEEK_SET);
    }

    // recv ack from client
    bzero(buf, BUFSIZE);
    n = recvfrom(*sockfd, buf, BUFSIZE, 0,
      (struct sockaddr *) (&(*clientaddr)), &(*clientlen));
    if (n < 0) {
      error("ERROR in recvfrom");
      return -1;
    }
    
  }
  printf("%ld\n", total);
    
  fclose(fp);
  return 1;
}

int put_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen
) {
  // get filename
  char fn[100];
  memcpy(fn, buf, n);
  fn[n] = '\0';

  // move pointer back to beginning
  buf -= 4;

  FILE *fp;
  fp = fopen(fn, "w");

  if(fp == NULL) {
    printf("File could not be opened\n");
    return -1;
  }
  printf("true");

  // recv either failed msg or file length
  bzero(buf, BUFSIZE);
  n = recvfrom(*sockfd, buf, BUFSIZE, 0,
    (struct sockaddr *) clientaddr, &(*clientlen));
  if (n < 0) {
    error("ERROR in recvfrom");
    return -1;
  }  
  char *remaining;
  long len = strtol(buf, &remaining, 10);
  printf("%ld\n", len);

  long total = 0;

  while(total < len) {
    // recv data from client
    bzero(buf, BUFSIZE);
    n = 0;
    n = recvfrom(*sockfd, buf, BUFSIZE, 0,
      (struct sockaddr *) clientaddr, &(*clientlen));
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
      (struct sockaddr *) &(*clientaddr), *clientlen);
    if (n < 0) {
      error("ERROR in sendto");
      return -1;
    }
  }
    
  fclose(fp);
  return 1;
}

int del_func(char *buf, int n, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen) {
  char fn[100];
  memcpy(fn, buf, n);
  fn[n] = '\0';

  char *msg;
  int ret_val = -1;

  if(remove(fn) == 0) {
    msg = "Delete successful";
    ret_val = 0;
  }
  else {
    msg = "Delete failed";
  }

  int m;
  m = sendto(*sockfd, msg, strlen(msg), 0, 
        (struct sockaddr *) &(*clientaddr), *clientlen);
  if (m < 0) {
    error("ERROR in sendto");
    return -1;
  }
  return ret_val;
}

int ls_func(char *buf, int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen) {
  bzero(buf, BUFSIZE); // clear buf
  char *st = buf;
  int s = 0;

  DIR *d;
  struct dirent *dir;
  d = opendir(".");
  if (d) {
    while ((dir = readdir(d)) != NULL) {
      int dir_len = strlen(dir->d_name);

      printf("%s\n", dir->d_name);
      memcpy(buf, dir->d_name, dir_len);
      buf[dir_len] = '\n'; // append new line char to end of each dir name
      buf += dir_len + 1;
      // printf("%s\n", st);
    }
    closedir(d);
    
  }
  else {
    return(-1);
  }
  /* 
  * sendto: send directory layout to the client 
  */
  int n;
  n = sendto(*sockfd, st, strlen(st), 0, 
        (struct sockaddr *) &(*clientaddr), *clientlen);
  if (n < 0) {
    error("ERROR in sendto");
    return -1;
  }
  // printf("%d\n", n);

  return(0);
}

int exit_func(int *sockfd, struct sockaddr_in *clientaddr,
  int *clientlen) {

  char msg[] = "Goodbye!";

  /* 
  * sendto: send goodbye to the client 
  */
  int n;
  n = sendto(*sockfd, msg, strlen(msg), 0, 
        (struct sockaddr *) &(*clientaddr), *clientlen);
  if (n < 0) {
    error("ERROR in sendto");
    return -1;
  }

  printf("%d", n);

  return(0);
}