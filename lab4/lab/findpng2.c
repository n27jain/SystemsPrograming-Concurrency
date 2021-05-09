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
sem_t items;
pthread_mutex_t stack_mutex;

ENTRY* entries;
int entries_size = 1000;
int entries_top = 0;
pthread_mutex_t hashmap_mutex;

int active_thread_count = 0;
pthread_mutex_t mutex;

int init_globals() {
  curl_global_init(CURL_GLOBAL_DEFAULT);
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

  if (pthread_mutex_init(&stack_mutex, NULL) != 0) {
    fprintf(stderr, "error: failed to create stack mutex\n");
    return -1;
  }
  if (pthread_mutex_init(&hashmap_mutex, NULL) != 0) {
    fprintf(stderr, "error: failed to create hashmap mutex\n");
    return -1;
  }
  if (pthread_mutex_init(&mutex, NULL) != 0) {
    fprintf(stderr, "error: failed to create mutex\n");
    return -1;
  }
  if (sem_init(&items, 0, 0) != 0) {
    fprintf(stderr, "error: failed to initialize semaphore\n");
    return -1;
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
  pthread_mutex_destroy(&stack_mutex);
  pthread_mutex_destroy(&hashmap_mutex);
  pthread_mutex_destroy(&mutex);
  sem_destroy(&items);
  for (int i = 0; i < 1000; i++) {
    free(entries[i].key);
  }
  hdestroy();
  xmlCleanupParser();
  free(entries);
}

void push_url(char * url) {
  pthread_mutex_lock(&stack_mutex);
  if (url_stack_top >= url_stack_size) {
    url_stack_size *= 2;
    url_stack = realloc(url_stack, url_stack_size * sizeof(char*));
  }
  url_stack[url_stack_top] = malloc(strlen(url) + 1);
  memcpy(url_stack[url_stack_top], url, strlen(url) + 1);
  url_stack_top++;
  pthread_mutex_unlock(&stack_mutex);
  sem_post(&items);
}

char* pop_url() {
  pthread_mutex_lock(&stack_mutex);
  if (url_stack_top <= 0) {
    return NULL;
  }
  url_stack_top--;
  char* tmp = url_stack[url_stack_top];
  url_stack[url_stack_top] = NULL;
  pthread_mutex_unlock(&stack_mutex);
  return tmp;
}

bool is_visited(char* url) {
  pthread_mutex_lock(&hashmap_mutex);
  ENTRY item;
  item.key = url;
  bool visited = hsearch(item, FIND) != NULL; 
  pthread_mutex_unlock(&hashmap_mutex);
  return visited;
}

void push_visited(char* url) {
  pthread_mutex_lock(&hashmap_mutex);
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
  pthread_mutex_unlock(&hashmap_mutex);
}

bool url_stack_empty() {
  pthread_mutex_lock(&stack_mutex);
  bool result = url_stack_top == 0;
  pthread_mutex_unlock(&stack_mutex);
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
        pthread_mutex_lock(&mutex);
        int num = num_found++;
        pthread_mutex_unlock(&mutex);
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

bool should_exit() {
  pthread_mutex_lock(&mutex);
  int num = num_found;
  bool no_more_urls = active_thread_count <= 0 && url_stack_empty();
  pthread_mutex_unlock(&mutex);
  return num >= m || no_more_urls;
}

void* thread_loop(void* ptr) {
  request_t req;
  CURLcode res;

  if (init_request(&req) != 0) {
    status = -1;
    return NULL;
  }

  while (1) {
    if (should_exit()) {
      break;
    }
    int val;
    sem_getvalue(&items, &val);
    PRINTF("SEM VAL: %d\n", val);
    sem_wait(&items);
    if (should_exit()) {
      break;
    }
    pthread_mutex_lock(&mutex);
    char* url = pop_url();
    if (url != NULL) {
      active_thread_count++;
      if (write_log) {
        fprintf(logfile, "%s\n", url);
      }
    }
    pthread_mutex_unlock(&mutex);
    if (url == NULL) {
      break;
    }
    set_request_url(&req, url);
    res = request_fetch(&req);
    if (res != CURLE_OK) {
      FPRINTF(stderr, "error: failed to fetch url: %s\n", url);
    } else {
      process_data(&req);
    }
    
    push_visited(url);
    pthread_mutex_lock(&mutex);
    active_thread_count--;
    pthread_mutex_unlock(&mutex);
    free(url);
  }

  sem_post(&items);

  cleanup_request(&req);


  return NULL;
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

  pthread_t tids[t];

  for (i = 0; i < t; i++) {
    if (pthread_create(&tids[i], NULL, thread_loop, NULL) != 0) {
      fprintf(stderr, "error: failed to create thread\n");
      return -1;
    }
  }

  for (i = 0; i < t; i++) {
    pthread_join(tids[i], NULL);
  }

  thread_loop(NULL);

  struct timeval t1;
  gettimeofday(&t1, 0);

  printf("findpng2 execution time: %f seconds\n", (float) (t1.tv_sec - t0.tv_sec) + (float) (t1.tv_usec - t0.tv_usec) / 1000000.0f);

  clean_globals();
  return status;
}

