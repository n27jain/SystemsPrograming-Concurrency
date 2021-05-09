#include "lab_net.h"

bool cancel_all_requests = false;

size_t request_header_handler(char *p_recv, size_t size, size_t nmemb, void *userdata)
{
  int realsize = size * nmemb; 
  RECV_BUF *p = userdata;
  
  if (realsize > strlen(ECE252_HEADER) &&
    strncmp(p_recv, ECE252_HEADER, strlen(ECE252_HEADER)) == 0) {

    /* extract img sequence number */
    p->seq = atoi(p_recv + strlen(ECE252_HEADER));
  }
  if (cancel_all_requests) {
    return CURLE_WRITE_ERROR;
  }
  return realsize;
}

size_t request_write_handler(char *p_recv, size_t size, size_t nmemb, void *p_userdata)
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

int request_xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
	if (cancel_all_requests) {
		return 1;
	} else {
		return 0;
	}
}

int request_recv_buf_init(RECV_BUF *ptr, size_t max_size)
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

int request_recv_buf_cleanup(RECV_BUF *ptr)
{
	if (ptr == NULL) {
	  return 1;
	}
	
	free(ptr->buf);
	ptr->size = 0;
	ptr->max_size = 0;
	return 0;
}

int init_request(request_t* req) {
  req->curl = curl_easy_init();
  if (req->curl == NULL) {
    return -1;
  } else {
    request_recv_buf_init(&req->recv_buf, BUF_SIZE);
		curl_easy_setopt(req->curl, CURLOPT_NOPROGRESS, 0);
		curl_easy_setopt(req->curl, CURLOPT_XFERINFOFUNCTION, request_xferinfo);

    /* register write call back function to process received data */
    curl_easy_setopt(req->curl, CURLOPT_WRITEFUNCTION, request_write_handler);
    /* user defined data structure passed to the call back function */
		curl_easy_setopt(req->curl, CURLOPT_WRITEDATA, (void *)&req->recv_buf);

    /* register header call back function to process received header data */
		curl_easy_setopt(req->curl, CURLOPT_HEADERFUNCTION, request_header_handler);
    /* user defined data structure passed to the call back function */
		curl_easy_setopt(req->curl, CURLOPT_HEADERDATA, (void *)&req->recv_buf);

    /* some servers requires a user-agent field */
    curl_easy_setopt(req->curl, CURLOPT_USERAGENT, "ece252 lab4 crawler");

    /* follow HTTP 3XX redirects */
    curl_easy_setopt(req->curl, CURLOPT_FOLLOWLOCATION, 1L);
    /* continue to send authentication credentials when following locations */
    curl_easy_setopt(req->curl, CURLOPT_UNRESTRICTED_AUTH, 1L);
    /* max numbre of redirects to follow sets to 5 */
    curl_easy_setopt(req->curl, CURLOPT_MAXREDIRS, 5L);
    /* supports all built-in encodings */ 
    curl_easy_setopt(req->curl, CURLOPT_ACCEPT_ENCODING, "");

    /* Max time in seconds that the connection phase to the server to take */
    //curl_easy_setopt(req->curl, CURLOPT_CONNECTTIMEOUT, 5L);
    /* Max time in seconds that libcurl transfer operation is allowed to take */
    //curl_easy_setopt(req->curl, CURLOPT_TIMEOUT, 10L);
    /* Time out for Expect: 100-continue response in milliseconds */
    //curl_easy_setopt(req->curl, CURLOPT_EXPECT_100_TIMEOUT_MS, 0L);

    /* Enable the cookie engine without reading any initial cookies */
    curl_easy_setopt(req->curl, CURLOPT_COOKIEFILE, "");
    /* allow whatever auth the proxy speaks */
    curl_easy_setopt(req->curl, CURLOPT_PROXYAUTH, CURLAUTH_ANY);
    /* allow whatever auth the server speaks */
    curl_easy_setopt(req->curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(req->curl, CURLOPT_VERBOSE, 0L);
  }
  return 0;
}

void set_request_url(request_t* req, const char* url) {
  curl_easy_setopt(req->curl, CURLOPT_URL, url);
  curl_easy_setopt(req->curl, CURLOPT_PRIVATE, url);
}

CURLcode request_fetch(request_t* req) {
  req->recv_buf.size = 0;
  return curl_easy_perform(req->curl);
}

void cleanup_request(request_t* req) {
  curl_easy_cleanup(req->curl);
	request_recv_buf_cleanup(&req->recv_buf);
}

void cancel_requests() {
  cancel_all_requests = true;
}
