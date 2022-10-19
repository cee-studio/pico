#include "httpd.h"
int main(int c, char **argv){
  char *port = argv[1];
  serve_forever(port);
  return 0;
}

void route(){
  if( strcmp("/", uri) == 0 ){
    printf("HTTP/1.1 200 OK\n");
    printf("Access-Control-Allow-Origin: *\n");
    //printf("Content-Length: %d\n", strlen(resp));
    printf("Cache-Control: no-cache\n\n");

    read_file("index.html");
  }else{
    char *file = uri + 1;
    if( does_file_exist(file) ){
      printf("HTTP/1.1 200 OK\n");
      printf("Access-Control-Allow-Origin: *\n");
      //printf("Content-Length: %d\n", strlen(resp));
      printf("Cache-Control: no-cache\n\n");
      read_file(file);
    }else{
      printf("HTTP/1.1 404 Not found\n\n");
    }
  }
}
