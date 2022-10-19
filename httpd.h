#ifndef _HTTPD_H___
#define _HTTPD_H___

#include <stdio.h>
#include <string.h>

// Client request
extern char *method, // "GET" or "POST"
    *uri,            // "/index.html" things before '?'
    *qs,             // "a=1&b=2" things after  '?'
    *prot,           // "HTTP/1.1"
    *payload;        // for POST

extern int payload_size;

// Server control functions
void serve_forever(const char *PORT);

char *request_header(const char *name);

#define HEADER_MAX 32
typedef struct {
  char *name, *value;
} header_t;
static header_t reqhdr[HEADER_MAX + 1] = {{"\0", "\0"}};
header_t *request_headers(void);

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
#endif
