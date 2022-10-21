#ifndef _HTTPD_H___
#define _HTTPD_H___

#include "picohttpparser.h"
#include <stdio.h>
#include <string.h>


// Server control functions
void serve_forever(const char *PORT);


// user shall implement this function
void route();

// Response
#define HTTP11 "HTTP/1.1"
#define CORS "Access-Control-Allow-Origin: *"

#define HTTP_200 printf(HTTP11" 200 OK\n\n")
#define HTTP_201 printf(HTTP11" 201 Created\n\n")
#define HTTP_404 printf(HTTP11" 404 Not found\n\n")
#define HTTP_500 printf(HTTP11" 500 Internal Server Error\n\n")

// some interesting macro for `route()`
#define ROUTE_START() if (0) {
#define ROUTE(METHOD, URI)                                                     \
  }                                                                            \
  else if (strcmp(URI, uri) == 0 && strcmp(METHOD, method) == 0) {
#define GET(URI) ROUTE("GET", URI)
#define POST(URI) ROUTE("POST", URI)
#define ROUTE_END()                                                            \
  }                                                                            \
  else HTTP_500;

extern int does_file_exist(const char *file_name);
extern int read_file(const char *file_name);

extern int debug_httpd;


struct sized_buffer {
  size_t size;
  char *start;
};

struct request_info {
  int pret;
  char *buf;
  int minor_version;
  struct phr_header headers[128];
  struct sized_buffer method, uri, payload;
  size_t buflen, prevbuflen, num_headers;
};

struct response_info {
  int socket;
  struct sized_buffer sb;
  char * end_of_data;
  struct phr_header headers[128];
};

extern struct request_info req_info;
extern struct response_info res_info;

extern void add_header(struct response_info *res, char *name, char *fmt, ...);
extern void terminate_headers(struct response_info *res);
extern void set_http_code(struct response_info *res, int code);
extern void send_response_binary(struct response_info *res, char *data, size_t data_len);
extern void send_response(struct response_info *res, char *text);
#endif
