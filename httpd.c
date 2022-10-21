#include "httpd.h"
#include "picohttpparser.h"
#include "HttpStatusCodes_C.h"

#include <stdarg.h>
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
#define BUF_SIZE (64*10*1024)
#define QUEUE_SIZE (1024*1024)

static int listenfd;
int *clients;
static void start_server(const char *);
static void respond(int);


struct request_info req_info = {};
struct response_info res_info = {};

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
    }else if( debug_httpd ) {
      //close(listenfd);
      respond(slot);
      close(clients[slot]);
      clients[slot] = -1;
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

/* unescape this in place */
static void uri_unescape(struct sized_buffer *sb){
  char chr = 0;
  char *src = sb->start, *dst;

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
void respond(int slot){
  ssize_t rret;
  int sock = clients[slot];
  char *buf = malloc(BUF_SIZE);
  memset(&req_info, 0, sizeof(req_info));
  req_info.buf = buf;
  req_info.num_headers = sizeof (req_info.headers) / sizeof (req_info.headers[0]);

  memset(&res_info, 0, sizeof(res_info));
  res_info.sb.start = malloc(BUF_SIZE);
  res_info.sb.size = BUF_SIZE;
  res_info.socket = sock;

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
  route();
  fflush(stderr);
  fflush(stdout);
  free(res_info.sb.start);
  free(req_info.buf);
}


int debug_httpd = 0;
#define res_buf_size(res)   (res->sb.size - (res->end_of_data - res->sb.start))

void add_header(struct response_info *res, char *name, char *value_fmt, ...){
  va_list arg;
  int i;
  for( i = 0; res->headers[i].name_len; i++ );

  res->headers[i].name = res->end_of_data;
  res->headers[i].name_len = snprintf(res->end_of_data, res_buf_size(res), "%s: ", name);
  res->end_of_data += res->headers[i].name_len;

  res->headers[i].value = res->end_of_data;
  va_start(arg, value_fmt);
  res->headers[i].value_len = vsnprintf(res->end_of_data, res_buf_size(res), value_fmt, arg);
  va_end(arg);
  res->end_of_data += res->headers[i].value_len;
  /* add '\n' to terminate this header */
  res->end_of_data[0] = '\n';
  res->end_of_data ++;
}

void terminate_headers(struct response_info *res){
  res->end_of_data[0] = '\n';
  res->end_of_data ++;
}

void set_http_code(struct response_info *res, int code){
  /* this has to be the first call */
  size_t n = snprintf(res->sb.start, res->sb.size,
                      "HTTP/1.1 %d %s\n", code, HttpStatus_reasonPhrase(code));
  res->end_of_data = res->sb.start + n;
}

void send_response_binary(struct response_info *res, char *data, size_t data_len){
  add_header(res, "Content-Length", "%d", data_len);
  terminate_headers(res);
  res->end_of_data +=
    snprintf(res->end_of_data, res_buf_size(res), "%.*s", data_len, data);

  printf("\n%.*s", res->end_of_data - res->sb.start, res->sb.start);
  int ret = send(res->socket, res->sb.start, res->end_of_data - res->sb.start, 0);
  if( ret == - 1){
    perror("send");
  }
  printf("\nsend %d bytes\n", ret);
}

void send_response(struct response_info *res, char *text){
  send_response_binary(res, text, strlen(text));
}

void send_sse_stream(struct response_info *res,
                     sg_read_cb read_db, void *handle,
                     sg_free_cb free_cb){
  add_header(res, "Content-Type", "text/event-stream");
  terminate_headers(res);
  int ret = send(res->socket, res->sb.start, res->end_of_data - res->sb.start, 0);
  if( ret == - 1){
    perror("send");
  }
  ssize_t read_size = 0;
  while( (read_size = read_db(handle, 0, res->sb.start, res->sb.size)) > 0 ){
    ret = send(res->socket, res->sb.start, read_size, 0);
  }
}