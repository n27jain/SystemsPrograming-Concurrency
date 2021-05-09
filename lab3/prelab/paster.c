#include "lab_png.h"

#include <stdlib.h>
#include <unistd.h>
#include <curl/curl.h>
#include <pthread.h>

#define ARG_ERR "error: invalid arguments, expected: paster [-t N] [-n M]\n"
#define SERVER_URL_FORMAT "http://ece252-%d.uwaterloo.ca:2520/image?img=%d"
#define ECE252_HEADER "X-Ece252-Fragment: "
#define BUF_SIZE 1048576	/* 1024*1024 = 1M */
#define BUF_INC	524288

#define NUM_IMAGES 50

#define max(a, b) \
	 ({ __typeof__ (a) _a = (a); \
			 __typeof__ (b) _b = (b); \
		 _a > _b ? _a : _b; })

#ifdef DEBUG_1
#	define PRINTF(...) printf(__VA_ARGS__);
#	define FPRINTF(...) fprintf(__VA_ARGS__);
#else
#	define PRINTF(...)
#	define FPRINTF(...)
#endif

typedef struct recv_buf2 {
  char *buf;			 /* memory to hold a copy of received data */
  size_t size;		 /* size of valid data in buf in bytes*/
  size_t max_size; /* max capacity of buf in bytes*/
  int seq;				 /* >=0 sequence number extracted from http header */
										 /* <0 indicates an invalid seq number */
} RECV_BUF;

/* Globals */

int image_number = 1;
struct PNG pngs[NUM_IMAGES];
bool loaded[NUM_IMAGES];
int load_count = 0;
int ret = 0;
pthread_mutex_t* load_mutex[NUM_IMAGES];
bool done = false;

size_t header_handler(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
  int realsize = size * nmemb;
  RECV_BUF *p = userdata;
  
  if (realsize > strlen(ECE252_HEADER) &&
    strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

    /* extract img sequence number */
    p->seq = atoi(p_recv + strlen(ECE252_HEADER));
  }
  if (done) {
    return CURLE_WRITE_ERROR;
  }
  return realsize;
}

size_t write_handler(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
{
  size_t realsize = size * nmemb;
  RECV_BUF *p = (RECV_BUF *)p_userdata;

  if (p->size + realsize + 1 > p->max_size) {/* hope this rarely happens */ 
    /* received data is not 0 terminated, add one byte for terminating 0 */
    size_t new_size = p->max_size + max(BUF_INC, realsize + 1);	 
    char *q = realloc(p->buf, new_size);
    if (q == NULL) {
        perror("realloc"); /* out of memory */
        return -1;
    }
    p->buf = q;
    p->max_size = new_size;
  }

  memcpy(p->buf + p->size, p_recv, realsize); /*copy data from libcurl*/
  p->size += realsize;
  p->buf[p->size] = 0;

  return realsize;
}

int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	if (done) {
		return 1;
	} else {
		return 0;
	}
}

int recv_buf_init(RECV_BUF *ptr, size_t max_size)
{
  void *p = NULL;
  
  if (ptr == NULL) {
      return 1;
  }

  p = malloc(max_size);
  if (p == NULL) {
    return 2;
  }
  
  ptr->buf = p;
  ptr->size = 0;
  ptr->max_size = max_size;
  ptr->seq = -1;							/* valid seq should be non-negative */
  return 0;
}

int recv_buf_cleanup(RECV_BUF *ptr)
{
	if (ptr == NULL) {
	  return 1;
	}
	
	free(ptr->buf);
	ptr->size = 0;
	ptr->max_size = 0;
	return 0;
}

/* Thread loop */

void * thread_loop(void * ptr) {
	CURL* curl_handle;
	CURLcode res;
	RECV_BUF recv_buf;
		
	recv_buf_init(&recv_buf, BUF_SIZE);

	/* init a curl session */
	curl_handle = curl_easy_init();

	if (curl_handle == NULL) {
		fprintf(stderr, "error: failed to initialize curl\n");
		ret = -1;
	} else {
		/* specify URL to get */
		curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_handler);
		curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&recv_buf);
		curl_easy_setopt(curl_handle, CURLOPT_HEADERFUNCTION, header_handler);
		curl_easy_setopt(curl_handle, CURLOPT_HEADERDATA, (void *)&recv_buf);
		curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, xferinfo);
		curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    char* url = malloc(sizeof(SERVER_URL_FORMAT)); 
	
		while (load_count < NUM_IMAGES && ret == 0) {
      sprintf(url, SERVER_URL_FORMAT, (rand() % 3) + 1, image_number);
		  curl_easy_setopt(curl_handle, CURLOPT_URL, url);
			recv_buf.size = 0;
			res = curl_easy_perform(curl_handle);
			if (res != CURLE_OK && !done) {
				fprintf(stderr, "error: failed to fetch image: %s\n", curl_easy_strerror(res));
			} else if (!done) {
				PRINTF("%lu bytes received in memory %p, seq=%d, from url: %s\n", recv_buf.size, recv_buf.buf, recv_buf.seq, url);
				while (pthread_mutex_trylock(load_mutex[recv_buf.seq % NUM_IMAGES]) != 0);
				if (recv_buf.seq >= 0 && !loaded[recv_buf.seq % NUM_IMAGES]) {
					loaded[recv_buf.seq % NUM_IMAGES] = true;
					pthread_mutex_unlock(load_mutex[recv_buf.seq % NUM_IMAGES]);
					if (load_png_from_memory((U8*) recv_buf.buf, recv_buf.size, &pngs[recv_buf.seq % NUM_IMAGES]) != 0) {
						fprintf(stderr, "error: received invalid image\n");
            free_png_data(&pngs[recv_buf.seq % NUM_IMAGES]);
            loaded[recv_buf.seq % NUM_IMAGES] = false;
					} else {
            load_count++;
            done = load_count >= 50;
					  PRINTF("loaded %d/%d.\n", load_count, NUM_IMAGES);
          }
				} else {
					pthread_mutex_unlock(load_mutex[recv_buf.seq % NUM_IMAGES]);
				}
			}
		}
    free(url);
	}
	
	/* cleaning up */
	curl_easy_cleanup(curl_handle);
	recv_buf_cleanup(&recv_buf);
	return NULL;
}

int main(const int argc, char * const * argv) {
	int c;
	int t = 1;
	while ((c = getopt (argc, argv, "t:n:")) != -1) {
		switch (c) {
		case 't':
			t = strtoul(optarg, NULL, 10);
			if (t <= 0) {
        fprintf(stderr, ARG_ERR);
        return -1;
      }
			break;
		case 'n':
      image_number = strtoul(optarg, NULL, 10);
      if (image_number <= 0 || image_number > 3) {
        fprintf(stderr, ARG_ERR);
        return -1;
      }
      break;
		default:
      fprintf(stderr, ARG_ERR);
			return -1;
	  }
	}
	
	curl_global_init(CURL_GLOBAL_DEFAULT);
	
	memset(loaded, false, NUM_IMAGES);
	
	pthread_t* threads = malloc(t * sizeof(pthread_t));
	int i;
	
	for (i = 0; i < NUM_IMAGES; i++) {
		load_mutex[i] = malloc(sizeof(pthread_mutex_t));
		if (pthread_mutex_init(load_mutex[i], NULL) != 0) {
			fprintf(stderr, "error: failed to create mutex: %d\n", i);
			ret = -1;
			break;
		}
	}
	
	if (ret == 0) {
		for (i = 0; i < t; i++) {
			if (pthread_create(&threads[i], NULL, thread_loop, NULL) != 0) {
				fprintf(stderr, "error: failed to create thread #%d\n", i);
				ret = -1;
			}
		}
		
		for (i = 0; i < t; i++) {
			pthread_join(threads[i], NULL);
		}
		
		for (i = 0; i < NUM_IMAGES; i++) {
			if (pthread_mutex_destroy(load_mutex[i]) != 0) {
				fprintf(stderr, "error: failed to destroy mutex: %d", i);
				ret = -1;
			}
		}
	}
	
	for (i = 0; i < NUM_IMAGES; i++) {
		free(load_mutex[i]);
	}
	free(threads);
	
	if (ret == 0) {
		struct PNG out;
		memcpy(&out.IHDR, &pngs[0].IHDR, DATA_IHDR_SIZE);
		for (i = 1; i < NUM_IMAGES; i++) {
			if (pngs[i].IHDR.width != out.IHDR.width) {
				fprintf(stderr, "error: invalid response, images width's are inconsistent\n");
				ret = -1;
				break;
			}
			if (pngs[i].idat_length > 1) {
				fprintf(stderr, "error: invalid response, expected single IDAT PNG\n");
			}
			out.IHDR.height += pngs[i].IHDR.height;
		}
		if (ret == 0) {
			out.p_IDAT = malloc(sizeof(struct chunk));
			out.idat_length = 1;
			out.p_IDAT->length = (1 + 4 * out.IHDR.width) * out.IHDR.height;
			out.p_IDAT->p_data = malloc(out.p_IDAT->length);
			memcpy(out.p_IDAT->type, "IDAT", 4);
			U8* ptr = out.p_IDAT->p_data;
			int decomptotal = 0;
			U64 decompressed_length;
			for (i = 0; i < NUM_IMAGES; i++) {
				if (mem_inf(ptr, &decompressed_length, pngs[i].p_IDAT->p_data, pngs[i].p_IDAT->length) == 0) {
					ptr += decompressed_length;
					decomptotal += decompressed_length;
				} else {
					fprintf(stderr, "error: failed to decompress PNG data for image: %d\n", i);
					ret = -1;
					break;
				}
			}
			if (ret == 0) {
				U8* data = malloc(out.p_IDAT->length);
				U64 compressed_length = 0;
				if (mem_def(data, &compressed_length, out.p_IDAT->p_data, out.p_IDAT->length, Z_DEFAULT_COMPRESSION) == 0) {
					free(out.p_IDAT->p_data);
					out.p_IDAT->p_data = data;
					out.p_IDAT->length = compressed_length;
					if (write_png_file("all.png", &out) == 0) {
						PRINTF("successfully wrote: all.png\n");
					} else {
						fprintf(stderr, "error: failed to write png file: all.png\n");
						ret = -1;
					}
				} else {
					fprintf(stderr, "error: failed to compress concatenated image\n");
					ret = -1;
					free(data);
				}
			}
			free_png_data(&out);
		}
	}

	for (i = 0; i < NUM_IMAGES; i++) {
		if (loaded[i]) {
			free_png_data(&pngs[i]);
		}
	}
	
	curl_global_cleanup();
	
	return 0;
}
