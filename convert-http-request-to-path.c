#include<stdio.h>
#include<stdlib.h>
#include<string.h>

const char* DEFAULT_FILE = "index.html";

char *to_path(char *req){
  char *start, *end;

  for(start = req; start[0]!= ' '; start++){
    if(!start[0]){
      return NULL;
    }
  }

    start++; // Skip over the space
             

    for(end = start; end[0] != ' '; end++){
      if(!end[0]){
        return NULL;
      }
    }

    // here now end[0] = ' '
    
    if(end[-1] != '/'){
      end[0] = '/';
      end++;
    }


    // If there isn't enough room to copy in "index.html" then return NULL.
    
    if(end + strlen(DEFAULT_FILE) > req + strlen(req)){
      return NULL;
    }

    memcpy(end, DEFAULT_FILE, strlen(DEFAULT_FILE) + 1);

    return start + 1; // Skip the leading '/' (e.g. in "/blog/index.html")
}


int main(){
 char req[] = "GET /blog ";
 char req1[] = "GET /blog HTTP/1.1\nHost: example.com";
 char req2[] = "GET /blog/ HTTP/1.1\nHost: example.com";
 char *path = to_path(req);
 printf("path is :%s\n",path);
  return 0;
}
