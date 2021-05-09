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
#include <search.h>

#define ARG_ERR "error: invalid arguments, expected: paster [-t N] [-n M]\n"
#define MAX_WAIT_MSECS (30*1000)

#ifdef DEBUG_1
#	define PRINTF(...) printf(__VA_ARGS__);
#	define FPRINTF(...) fprintf(__VA_ARGS__);
#else
#	define PRINTF(...)
#	define FPRINTF(...)
#endif

/* Globals */

int status;

bool write_log = false;
int t, m;
char* v;
int num_found;
FILE* outfile;
FILE* logfile;

char** url_stack;
size_t url_stack_size;
size_t url_stack_top;

ENTRY* entries;
int entries_size = 1000;
int entries_top = 0;

int init_globals() {
  curl_global_init(CURL_GLOBAL_ALL);
  status = 0;
  url_stack = calloc(10, sizeof(char*));
  url_stack_size = 10;
  url_stack_top = 0;
  num_found = 0;
  if ((outfile = fopen("png_urls.txt", "w")) == NULL) {
    fprintf(stderr, "error: failed to create/open png_urls.txt\n");
    return -1;
  }
  if (write_log) {
    if ((logfile = fopen(v, "w")) == NULL) {
      fprintf(stderr, "error: failed to create/open file: %s\n", v);
      return -1;
    }
  }
  if (!hcreate(1000)) {
    fprintf(stderr, "error: failed to create hashmap\n");
    return -1;
  }
  entries = malloc(1000 * sizeof(ENTRY));
  for (int i = 0; i < 1000; i++) {
    entries[i].key = NULL;
    entries[i].data = NULL;
  }

  xmlInitParser();

  return 0;
}

void clean_globals() {
  curl_global_cleanup();
  for (int i = 0; i < url_stack_top; i++) {
    free(url_stack[i]);
  }
  free(url_stack);
  fclose(outfile);
  if (write_log) {
    fclose(logfile);
  }
  for (int i = 0; i < entries_size; i++) {
    free(entries[i].key);
  }
  hdestroy();
  xmlCleanupParser();
  free(entries);
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
  ENTRY item;
  item.key = url;
  bool visited = hsearch(item, FIND) != NULL; 
  return visited;
}

void push_visited(char* url) {
  ENTRY item;
  item.key = malloc(strlen(url) + 1);
  memcpy(item.key, url, strlen(url) + 1);
  item.data = NULL;
  hsearch(item, ENTER);
  if (entries_top == entries_size) {
    entries = realloc(entries, entries_size * 2 * sizeof(ENTRY));
    entries_size *= 2;
    for (int i = entries_top; i < entries_size; i++) {
      entries[i].key = NULL;
      entries[i].data = NULL;
    }
  }
  entries[entries_top] = item;
  entries_top++;
}

bool url_stack_empty() {
  bool result = url_stack_top == 0;
  return result;
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
        int num = num_found++;
        if (num <= m) {
          if (fprintf(outfile, "%s\n", eurl) != 0) {
            return -1;
          }
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

int run_web_crawler() {
  CURLM* multi = curl_multi_init();
  request_t* reqs = malloc(t * sizeof(request_t));
  char** urls = malloc(t * sizeof(char*));
  memset(urls, 0, t * sizeof(char*));
  int* req_stack = malloc(t * sizeof(int));
  int req_stack_top = t;

  int i;
  for (i = 0; i < t; i++) {
    if (init_request(&reqs[i]) != 0) {
      status = -1;
      free(reqs);
      free(urls);
      free(req_stack);
      curl_multi_cleanup(multi);
      fprintf(stderr, "error: failed to create curl handle\n");
      return -1;
    }
    req_stack[i] = i;
  }

  int msgs_left = 0;
  int still_running = 0;

  while (status == 0 && num_found < m && !(url_stack_empty() && still_running == 0)) {
    char* url = NULL;
    while (req_stack_top > 0) {
      url = pop_url();
      if (url != NULL) {
        int index = req_stack[--req_stack_top];
        set_request_url(&reqs[index], url);
        free(urls[index]);
        urls[index] = url;
        reqs[index].recv_buf.size = 0;
        curl_multi_add_handle(multi, reqs[index].curl);
      } else {
        break;
      }
    }

    curl_multi_perform(multi, &still_running);

    int numfds;
    int res = curl_multi_wait(multi, NULL, 0, MAX_WAIT_MSECS, &numfds);
    if (res != CURLM_OK) {
      fprintf(stderr, "error: curl_multi_wait() returned %d\n", res);
      status = -1;
      break;
    }

    CURLMsg* msg = NULL;

    while ((msg = curl_multi_info_read(multi, &msgs_left)) != NULL) {
      if (msg->msg == CURLMSG_DONE) {
        CURL* eh = msg->easy_handle;

        int idx = -1;
        for (i = 0; i < t; i++) {
          if (eh == reqs[i].curl) {
            idx = i;
            break;
          }
        }

        req_stack[req_stack_top++] = idx;

        curl_multi_remove_handle(multi, eh);
        
        if (msg->data.result != CURLE_OK) {
          FPRINTF(stderr, "CURL error code: %d\n", msg->data.result);
          continue;
        }

        process_data(&reqs[idx]);

        push_visited(urls[idx]);

        if (write_log) {
          fprintf(logfile, "%s\n", urls[idx]);
        }
      }
      else {
        FPRINTF(stderr, "error: after curl_multi_info_read(), CURLMsg=%d\n", msg->msg);
        status = -1;
        break;
      }
    }
  }

  curl_multi_cleanup(multi);
  
  for (i = 0; i < t; i++) {
    cleanup_request(&reqs[i]);
    free(urls[i]);
  }

  free(reqs);
  free(urls);
  free(req_stack);

  return status;
}

int main(const int argc, char * const * argv) {
  int c;
	t = 1;
  m = 50;
  v = NULL;
  int i = 0;
	while ((c = getopt (argc, argv, "t:m:v:")) != -1) {
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
    case 'v':
      v = malloc(strlen(optarg) + 1);
      memcpy(v, optarg, strlen(optarg) + 1);
      write_log = true;
      i += 2;
      if (strlen(optarg) <= 0) {
        fprintf(stderr, ARG_ERR);
        return -1;
      }
      break;

		default:
      fprintf(stderr, ARG_ERR);
			return -1;
	  }
	}
  
  if (t < 1 || m <= 0 || i + 2 != argc) {
    fprintf(stderr, "error: invalid arguments, expected ./findpng2 [-t>=1] [-m>=1] <SEED_URL>\n");
    return -1;
  }

  if (init_globals() != 0) {
    return -1;
  }

  struct timeval t0;
  gettimeofday(&t0, 0);
  
  push_url(argv[argc-1]);

  run_web_crawler();

  struct timeval t1;
  gettimeofday(&t1, 0);

  printf("findpng2 execution time: %f seconds\n", (float) (t1.tv_sec - t0.tv_sec) + (float) (t1.tv_usec - t0.tv_usec) / 1000000.0f);

  clean_globals();
  return status;
}

