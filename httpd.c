#include "httpd.h"
#include "picohttpparser.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define MAX_CONNECTIONS 1024
#define BUF_SIZE (64*1024)
#define QUEUE_SIZE (1024*1024)

static int listenfd;
int *clients;
static void start_server(const char *);
static void respond(int);


struct request_info req_info = {0};

void serve_forever(const char *PORT) {
  struct sockaddr_in clientaddr;
  socklen_t addrlen;

  int slot = 0;

  const char *http_protocol = getenv("HTTP_PROTOCOL");
  const char *http_domain = getenv("HTTP_DOMAIN");

  if( http_domain && http_protocol )
    printf("Server started\n%s%s://%s.%s%s\n",
           "\033[92m", http_protocol, PORT, http_domain, "\033[0m");
  else
    printf("Server started\n%shttp://127.0.0.1:%s%s\n", "\033[92m", PORT, "\033[0m");

  // create shared memory for client slot array
  clients = mmap(NULL, sizeof(*clients) * MAX_CONNECTIONS,
                 PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);

  // Setting all elements to -1: signifies there is no client connected
  int i;
  for( i = 0; i < MAX_CONNECTIONS; i++ )
    clients[i] = -1;
  start_server(PORT);

  // Ignore SIGCHLD to avoid zombie threads
  signal(SIGCHLD, SIG_IGN);

  // ACCEPT connections
  while( 1 ){
    addrlen = sizeof(clientaddr);
    clients[slot] = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);

    if( clients[slot] < 0 ){
      perror("accept() error");
      exit(1);
    }else{
      if( fork() == 0 ){
        close(listenfd);
        respond(slot);
        close(clients[slot]);
        clients[slot] = -1;
        exit(0);
      }else{
        close(clients[slot]);
      }
    }

    while( clients[slot] != -1 )
      slot = (slot + 1) % MAX_CONNECTIONS;
  }
}

// start server
void start_server(const char *port) {
  struct addrinfo hints, *res, *p;

  // getaddrinfo for host
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  if( getaddrinfo(NULL, port, &hints, &res) != 0 ){
    perror("getaddrinfo() error");
    exit(1);
  }
  // socket and bind
  for( p = res; p != NULL; p = p->ai_next ){
    int option = 1;
    listenfd = socket(p->ai_family, p->ai_socktype, 0);
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
    if( listenfd == -1 )
      continue;
    if( bind(listenfd, p->ai_addr, p->ai_addrlen) == 0 )
      break;
  }
  if( p == NULL ){
    perror("socket() or bind()");
    exit(1);
  }

  freeaddrinfo(res);

  // listen for incoming connections
  if( listen(listenfd, QUEUE_SIZE) != 0 ){
    perror("listen() error");
    exit(1);
  }
}

static void uri_unescape(struct sized_buffer *sb){
  char chr = 0;
  char *src = sb->start;
  char *dst = sb->start;

  // Skip inital non encoded character
  while( *src
         && !isspace((int)(*src))
         && (*src != '%')
         && src - sb->start < sb->size )
    src++;

  // Replace encoded characters with corresponding code.
  dst = src;
  while( *src && !isspace((int)(*src)) && src - sb->start < sb->size ){
    if( *src == '+' )
      chr = ' ';
    else if( (*src == '%') && src[1] && src[2] ){
      src++;
      chr = ((*src & 0x0F) + 9 * (*src > '9')) * 16;
      src++;
      chr += ((*src & 0x0F) + 9 * (*src > '9'));
    }else
      chr = *src;
    *dst++ = chr;
    src++;
  }
  *dst = '\0';
}

// client connection
void respond(int slot) {
  printf("response %d\n", slot);
  ssize_t rret;
  int sock = clients[slot];
  char *buf = calloc(BUF_SIZE, 1);
  req_info.buf = buf;
  req_info.num_headers = sizeof (req_info.headers) / sizeof (req_info.headers[0]);

  //int pret;
  while( 1 ){
    while( (rret = recv(sock, buf + req_info.buflen, BUF_SIZE - req_info.buflen, 0)) == -1
           && errno == EINTR ) /* repeat */;
    if( rret <= 0 ) {
      printf("read error");
      return; // error
    }

    req_info.prevbuflen = req_info.buflen;
    req_info.buflen += rret;

    req_info.pret = phr_parse_request(buf,
                                      req_info.buflen,
                                      &req_info.method.start,
                                      &req_info.method.size,
                                      &req_info.uri.start,
                                      &req_info.uri.size,
                                      &req_info.minor_version,
                                      req_info.headers,
                                      &req_info.num_headers,
                                      req_info.prevbuflen);
    if( req_info.pret > 0 ){
      req_info.payload.start = buf + req_info.pret;
      req_info.payload.size = req_info.buflen - req_info.pret;
      break;
    }else if( req_info.pret == -1 ){
      printf("parsing error\n");
      return; // parsing error
    }else if( req_info.pret == -2 ){ // incomplete request
      if( req_info.buflen == BUF_SIZE ){
        printf("request is too long\n");
        return; // Request is too long
      }
    }
  }

  uri_unescape(&req_info.uri);
  printf("parsing request is %d bytes long\n", req_info.pret);
  printf("the whole request is %d bytes long\n", req_info.buflen);
  printf("method is %.*s\n", (int)req_info.method.size, req_info.method.start);
  printf("path is %.*s\n", (int)req_info.uri.size, req_info.uri.start);
  printf("HTTP version is 1.%d\n", req_info.minor_version);
  printf("headers:\n");
  for( int i = 0; i != req_info.num_headers; ++i ){
    printf("%.*s: %.*s\n", (int)req_info.headers[i].name_len,
           req_info.headers[i].name,
           (int)req_info.headers[i].value_len,
           req_info.headers[i].value);
  }
  printf("body:%.*s", req_info.payload.size, req_info.payload.start);
  fflush(stdout);

  // bind clientfd to stdout, making it easier to write
  int clientfd = clients[slot];
  dup2(clientfd, STDOUT_FILENO);
  close(clientfd);

  // call router
  route();

  // tidy up
  fflush(stdout);
  shutdown(STDOUT_FILENO, SHUT_WR);
  close(STDOUT_FILENO);
  //free(buf);
}


#define CHUNK_SIZE 1024 // read 1024 bytes at a time

int does_file_exist(const char *file_name) {
  struct stat buffer;
  int exists;

  exists = (stat(file_name, &buffer) == 0);

  return exists;
}

int read_file(const char *file_name) {
  char buf[CHUNK_SIZE];
  FILE *file;
  size_t nread;
  int err = 1;

  file = fopen(file_name, "r");

  if( file ){
    while ((nread = fread(buf, 1, sizeof buf, file)) > 0)
      fwrite(buf, 1, nread, stdout);

    err = ferror(file);
    fclose(file);
  }
  return err;
}

int debug_httpd = 0;