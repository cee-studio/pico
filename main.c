#include "httpd.h"
int main(int c, char **argv){
  char *port = argv[1];
  serve_forever(port);
  return 0;
}

void route(){
  set_http_code(&res_info, 200);
  add_header(&res_info, "Access-Control-Allow-Origin", "*");
  add_header(&res_info, "Cache-Control", "no-cache");
  send_response(&res_info, "Ok");
}
