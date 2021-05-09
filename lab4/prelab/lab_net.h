#pragma once

#include "lab_png.h"

#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>

#define BUF_SIZE 32768	/* 1024*1024 = 1M */
#define BUF_INC	524288

#define ECE252_HEADER "X-Ece252-Fragment: "

#define CT_PNG  "image/png"
#define CT_HTML "text/html"
#define CT_PNG_LEN  9
#define CT_HTML_LEN 9

#define max(a, b) \
	 ({ __typeof__ (a) _a = (a); \
			 __typeof__ (b) _b = (b); \
		 _a > _b ? _a : _b; })

typedef struct recv_buf2 {
  char *buf;			 /* memory to hold a copy of received data */
  size_t size;		 /* size of valid data in buf in bytes*/
  size_t max_size; /* max capacity of buf in bytes*/
  int seq;				 /* >=0 sequence number extracted from http header */
										 /* <0 indicates an invalid seq number */
} RECV_BUF;

typedef struct request {
  CURL* curl;
  RECV_BUF recv_buf;
} request_t;

extern bool cancel_all_requests;

size_t request_header_handler(char *p_recv, size_t size, size_t nmemb, void *userdata);
size_t request_write_handler(char *p_recv, size_t size, size_t nmemb, void *p_userdata);
int request_xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
int request_recv_buf_init(RECV_BUF *ptr, size_t max_size);
int request_recv_buf_cleanup(RECV_BUF *ptr);

int init_request(request_t* req);
void set_request_url(request_t* req, const char* url);
CURLcode request_fetch(request_t* req);
void cleanup_request(request_t* req);

void cancel_requests();
