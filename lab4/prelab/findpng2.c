#include "lab_net.h"
#include "lab_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>

#define ARG_ERR "error: invalid arguments, expected: paster [-t N] [-n M]\n"

#ifdef DEBUG_1
#	define PRINTF(...) printf(__VA_ARGS__);
#	define FPRINTF(...) fprintf(__VA_ARGS__);
#else
#	define PRINTF(...)
#	define FPRINTF(...)
#endif

/* Globals */

int status;

int t, m;
int num_found;
FILE* outfile;

char** url_stack;
size_t url_stack_size;
size_t url_stack_top;

char** visited_urls;
size_t visited_size;

int init_globals() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
  status = 0;
  url_stack = calloc(10, sizeof(char*));
  url_stack_size = 10;
  url_stack_top = 0;
  visited_urls = NULL;
  visited_size = 0;
  num_found = 0;
  if ((outfile =fopen("png_urls.txt", "w")) == NULL) {
    fprintf(stderr, "error: failed to create png_urls.txt\n");
    return -1;
  }
  return 0;
}

void clean_globals() {
  curl_global_cleanup();
  for (int i = 0; i < url_stack_top; i++) {
    free(url_stack[i]);
  }
  free(url_stack);
  fclose(outfile);
}

void push_url(char * url) {
  if (url_stack_top >= url_stack_size) {
    url_stack_size *= 2;
    url_stack = realloc(url_stack, url_stack_size * sizeof(char*));
  }
  url_stack[url_stack_top] = malloc(strlen(url) + 1);
  memcpy(url_stack[url_stack_top], url, strlen(url) + 1);
  url_stack_top++;
}

char* pop_url() {
  if (url_stack_top <= 0) {
    return NULL;
  }
  url_stack_top--;
  char* tmp = url_stack[url_stack_top];
  url_stack[url_stack_top] = NULL;
  return tmp;
}

bool is_visited(char* url) {
  if (visited_size == 0) {
    return false;
  }
  for (int i = 0; i < visited_size; i++) {
    if (strcmp(url, visited_urls[i]) == 0) {
      return true;
    }
  }
  return false;
}

void push_visited(char* url) {
  visited_urls = realloc(visited_urls, (++visited_size) * sizeof(char*));
  visited_urls[visited_size - 1] = malloc(strlen(url) + 1);
  memcpy(visited_urls[visited_size - 1], url, strlen(url) + 1);
}

bool url_stack_empty() {
  return url_stack_top == 0;
}


htmlDocPtr mem_getdoc(char *buf, int size, const char *url) {
    int opts = HTML_PARSE_NOBLANKS | HTML_PARSE_NOERROR | \
               HTML_PARSE_NOWARNING | HTML_PARSE_NONET;
    htmlDocPtr doc = htmlReadMemory(buf, size, url, NULL, opts);
    
    if ( doc == NULL ) {
        FPRINTF(stderr, "error: failed to parse document\n");
        return NULL;
    }
    return doc;
}

xmlXPathObjectPtr getnodeset(xmlDocPtr doc, xmlChar *xpath) {
	
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;

    context = xmlXPathNewContext(doc);
    if (context == NULL) {
        return NULL;
    }
    result = xmlXPathEvalExpression(xpath, context);
    xmlXPathFreeContext(context);
    if (result == NULL) {
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        return NULL;
    }
    return result;
}

int find_http(char *buf, int size, int follow_relative_links, const char *base_url) {
    int i;
    htmlDocPtr doc;
    xmlChar *xpath = (xmlChar*) "//a/@href";
    xmlNodeSetPtr nodeset;
    xmlXPathObjectPtr result;
    xmlChar *href;
		
    if (buf == NULL) {
        return 1;
    }

    doc = mem_getdoc(buf, size, base_url);
    result = getnodeset (doc, xpath);
    if (result) {
        nodeset = result->nodesetval;
        for (i=0; i < nodeset->nodeNr; i++) {
            href = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
            if ( follow_relative_links ) {
                xmlChar *old = href;
                href = xmlBuildURI(href, (xmlChar *) base_url);
                xmlFree(old);
            }
            if (href != NULL) {
              if (!is_visited((char*) href) && !strncmp((const char *)href, "http", 4)) {
                PRINTF("URL: %s\n", href);
                push_url((char*) href);
              }
            }
            xmlFree(href);
        }
        xmlXPathFreeObject (result);
    }
    xmlFreeDoc(doc);
    xmlCleanupParser();
    return 0;
}

int process_html(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    int follow_relative_link = 1;
    char *base_url = NULL; 

    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &base_url);
    return find_http(p_recv_buf->buf, p_recv_buf->size, follow_relative_link, base_url); 
}

int process_png(CURL *curl_handle, RECV_BUF *p_recv_buf) {
    char *eurl = NULL;          /* effective URL */
    curl_easy_getinfo(curl_handle, CURLINFO_EFFECTIVE_URL, &eurl);
    if ( eurl != NULL && is_png((unsigned char *) p_recv_buf->buf, p_recv_buf->size)) {
      if (!is_visited(eurl)) {
        PRINTF("The PNG url is: %s\n", eurl);
        num_found++;
        if (fprintf(outfile, "%s\n", eurl) != 0) {
          return -1;
        }
      }
    } else {
      FPRINTF(stderr, "INVALID IMAGE %s\n", eurl);
    }
    return 0;
}

int process_data(request_t *request) {
    CURLcode res;
    long response_code;

    res = curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &response_code);
    if ( res == CURLE_OK ) {
	    PRINTF("Response code: %ld\n", response_code);
    }

    if ( response_code >= 400 ) { 
    	FPRINTF(stderr, "error: bad request\n");
      return 1;
    }

    char *ct = NULL;
    res = curl_easy_getinfo(request->curl, CURLINFO_CONTENT_TYPE, &ct);
    if ( res == CURLE_OK && ct != NULL ) {
    	PRINTF("Content-Type: %s, len=%ld\n", ct, strlen(ct));
    } else {
      FPRINTF(stderr, "error: failed obtain Content-Type\n");
      return 2;
    }

    if ( strstr(ct, CT_HTML) ) {
      return process_html(request->curl, &request->recv_buf);
    } else if ( strstr(ct, CT_PNG) ) {
      return process_png(request->curl, &request->recv_buf);
    } else {
      /*sprintf(fname, "./output_%d", pid);*/
    }

    return 0; //write_file(fname, request->recv_buf->buf, request->recv_buf->size);
}

void* thread_loop(void* ptr) {
  request_t req;
  CURLcode res;

  if (init_request(&req) != 0) {
    status = -1;
    return NULL;
  }

  while (!url_stack_empty() && num_found < m) {
    char* url = pop_url();
    set_request_url(&req, url);
    res = request_fetch(&req);
    if (res != CURLE_OK) {
      FPRINTF(stderr, "error: failed to fetch url: %s\n", url);
    } else {
      process_data(&req);
    }
    
    push_visited(url);
    free(url);
  }

  cleanup_request(&req);

  return NULL;
}

int main(const int argc, char * const * argv) {
  int c;
	t = 1;
  m = 50;
  //char* v = NULL;
  int i = 0;
	while ((c = getopt (argc, argv, "t:m:")) != -1) {
		switch (c) {
		case 't':
			t = strtoul(optarg, NULL, 10);
      i += 2;
			if (t <= 0) {
        fprintf(stderr, ARG_ERR);
        return -1;
      }
			break;
		case 'm':
      m = strtoul(optarg, NULL, 10);
      i += 2;
      if (m <= 0) {
        fprintf(stderr, ARG_ERR);
        return -1;
      }
      break;
		default:
      fprintf(stderr, ARG_ERR);
			return -1;
	  }
	}
  
  if (t != 1 || m < 0 || i + 2 != argc) {
    fprintf(stderr, "error: invalid arguments, expected ./findpng2 [-t=1] [-m>1] <SEED_URL>\n");
    return -1;
  }

  if (init_globals() != 0) {
    return -1;
  }

  struct timeval t0;
  gettimeofday(&t0, 0);

  push_url(argv[argc-1]);
  thread_loop(NULL);

  struct timeval t1;
  gettimeofday(&t1, 0);

  printf("findpng2 execution time: %f seconds\n", (float) (t1.tv_sec - t0.tv_sec) + (float) (t1.tv_usec - t0.tv_usec) / 1000000.0f);

  clean_globals();
  return status;
}

