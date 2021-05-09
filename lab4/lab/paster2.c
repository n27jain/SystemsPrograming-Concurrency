#include "lab_net.h"

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

#define SERVER_URL_FORMAT "http://ece252-%d.uwaterloo.ca:2530/image?img=%d&part=%d"

#define NUM_IMAGE_SEGMENTS 50

const int TOTAL_PNG_SIZE = sizeof(struct PNG) + sizeof(struct data_IHDR) + sizeof(struct chunk) + BUF_SIZE;

#ifdef DEBUG_1
#	define PRINTF(...) printf(__VA_ARGS__);
#	define FPRINTF(...) fprintf(__VA_ARGS__);
#else
#	define PRINTF(...)
#	define FPRINTF(...)
#endif

/* Global */
int B, P, C, X, N;
int i = 0;

/* Shared memory IDs */
int shmid_seg_data;
int shmid_seg_size;
int shmid_spaces;
int shmid_items;
int shmid_produce_count;
int shmid_consume_count;
int shmid_status;
int shmid_mutex;
int shmid_producer_ready;
int shmid_segments;
int shmid_positions;

/* Shared */
unsigned char* segment_data;    // buffer data
int* segment_size;              // size of data
int* produce_count = 0;
int* consume_count = 0;
int* status = 0;
struct PNG* segments;
int * positions;
pthread_mutex_t* mutex;         // This handles usage of buffer by consumer / producer
sem_t* spaces;
sem_t* items;

int procTester(int n)		
{		
    usleep((n+1)*1000);
    PRINTF("Worker ID=%d, pid = %d, ppid = %d.\n", n, getpid(), getppid());		
    return 0;		
}

int shared_memory_attach() {
  segment_data = shmat(shmid_seg_data, NULL, 0);
  segment_size = shmat(shmid_seg_size, NULL, 0);
  spaces = shmat(shmid_spaces, NULL, 0);
  items = shmat(shmid_items, NULL, 0);
  produce_count = shmat(shmid_produce_count, NULL, 0);

  consume_count = shmat(shmid_consume_count, NULL, 0);
  status = shmat(shmid_status, NULL, 0);
  mutex = shmat(shmid_mutex, NULL, 0);
  
  segments = (struct PNG* )shmat(shmid_segments, NULL, 0);
  positions = shmat(shmid_positions, NULL, 0);
 
  for (i = 0; i < NUM_IMAGE_SEGMENTS; i++) {
	  segments[i].p_IDAT = NULL;
  }
  for(i=0; i< NUM_IMAGE_SEGMENTS; i++){
	  positions[i] = 0;
  }
 
  if (segment_data == (void *) -1
    || segment_size == (void *) -1
    || spaces == (void *) -1
    || items == (void *) -1
    || produce_count == (void *) -1
    || consume_count == (void *) -1
    || status == (void *) -1
    || mutex == (void *) -1
	  || segments == (void *) -1
	  || positions ==(void *) -1
	) {
    return -1;
  } else {
    return 0;
  }
}

void map_png_to_shm(struct PNG* png, int index) {
  segments[index] = *png;
  U8* ptr;
  ptr = (U8 *) segments + NUM_IMAGE_SEGMENTS * sizeof(struct PNG) + index * sizeof(struct chunk);
  memcpy(ptr, png->p_IDAT, sizeof(struct chunk));
  segments[index].p_IDAT = (struct chunk *) ptr;
  ptr = (U8 *) segments + NUM_IMAGE_SEGMENTS * (sizeof(struct PNG) + sizeof(struct chunk)) + index * BUF_SIZE;
  memcpy(ptr, png->p_IDAT->p_data, png->p_IDAT->length);
  segments[index].p_IDAT->p_data = ptr;
}

int producer(int produceid) {
  int pid = fork();
  if (pid == 0) {
    
    request_t request;
    CURLcode res;

    if (init_request(&request) != 0) {
      fprintf(stderr, "error: failed to initialize curl\n");
      *status = -1;
    } else {

      char* url = malloc(sizeof(SERVER_URL_FORMAT));

      int redo = -1;
      int segment;
    
      while (*status == 0) {
        if (NUM_IMAGE_SEGMENTS - *produce_count <= produceid && redo < 0) {
          break;
        }
        if (redo < 0) {
          pthread_mutex_lock(mutex);
          segment = (*produce_count)++;
          pthread_mutex_unlock(mutex);
        } else {
          segment = redo; 
        }
        if (segment >= NUM_IMAGE_SEGMENTS) {
          break;
        }
        sprintf(url, SERVER_URL_FORMAT, (rand() % 3) + 1, N, segment);
        set_request_url(&request, url);
        res = request_fetch(&request);
        if (res != CURLE_OK) {
          fprintf(stderr, "error: failed to fetch image: %s\n", curl_easy_strerror(res));
          redo = segment;
        } else {
          PRINTF("%lu bytes received in memory %p, seq=%d, from url: %s\n", request.recv_buf.size, request.recv_buf.buf, request.recv_buf.seq, url);
          sem_wait(spaces);
		      pthread_mutex_lock(mutex);
          memcpy(segment_data + (segment % B) * BUF_SIZE, request.recv_buf.buf, request.recv_buf.size); 		         
    		  // Take our segment data and based on the current image index place it in the appropriate area of memory		
    		  // Here BUF_SIZE will be the expected size of the retrieved PNG % B is use dto determine its place in the shared mem. 
          segment_size[segment % B] = request.recv_buf.size;
		      positions[segment] = 1;
          pthread_mutex_unlock(mutex);
          sem_post(items);
        }
      }
      free(url);
    }

    if (*status != 0) {
      sem_post(spaces);
      sem_post(items);
    }

    /* cleaning up */
    cleanup_request(&request);
    return pid;
  } else {
    return pid;
  }
}

int consumer(int consumeid) {
  int pid = fork();
  if (pid == 0) {
    int index;
    int count;
    while (*status == 0) {
      if (NUM_IMAGE_SEGMENTS - *consume_count <= consumeid) {
        break;
      }
      pthread_mutex_lock(mutex);
		  count = (*consume_count)++;
      pthread_mutex_unlock(mutex);

      if (count >= NUM_IMAGE_SEGMENTS) {
        break;
      }

      sem_wait(items);

      if (*status != 0) {
        break;
      }

      pthread_mutex_lock(mutex);
		  for (i = 0; i< NUM_IMAGE_SEGMENTS; i++){
			  if(positions[i] == 1){
				  positions[i] = 2; // set it to consumed flag so that we dont use this data again
				  index = i;
				  break;
			  }
		  }
		  U8* payload = segment_data + (index % B) * BUF_SIZE;
		  int size = segment_size[index % B];
      pthread_mutex_unlock(mutex);
	  
      struct PNG png;
      png.p_IDAT = NULL;
      if (load_png_from_memory(payload, size, &png) != 0) {
        fprintf(stderr, "error: received invalid image\n");
        *status = -1;
        break;
      } else {
        PRINTF("loaded %d/%d.\n", index+1, NUM_IMAGE_SEGMENTS);
        usleep(X * 1000);
        map_png_to_shm(&png, index);
      }
      free_png_data(&png);
      sem_post(spaces);
    }

    if (*status != 0) {
      sem_post(spaces);
      sem_post(items);
    }
    
    return pid;
  } else {
    return pid;
  }
}

int main(const int argc, char * const * argv) {
	if (argc != 6) {
    fprintf(stderr, "error: invalid arguments, expected ./paster2 <B> <P> <C> <X> <N>\n");
    return -1;
  }
  B = atoi(argv[1]);
  P = atoi(argv[2]);
  C = atoi(argv[3]);
  X = atoi(argv[4]);
  N = atoi(argv[5]);
  if (B < 1 || P <= 0 || P <= 0 || X < 0 || N < 1 || N > 3) {  // for prelab
		fprintf(stderr, "error: invalid arguments, expected ./paster2 <B >= 1> < P>=1 > < C>=1 > < X>=0 > < N >\n");	
		return -1;
  }

  // set up all of the shared memeory
  shmid_seg_data = shmget(IPC_PRIVATE, BUF_SIZE * B, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_seg_size = shmget(IPC_PRIVATE, sizeof(int) * B + BUF_SIZE * B, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_spaces = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_items = shmget(IPC_PRIVATE, sizeof(sem_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_produce_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_consume_count = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_status = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_mutex = shmget(IPC_PRIVATE, sizeof(pthread_mutex_t), IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_segments = shmget(IPC_PRIVATE, NUM_IMAGE_SEGMENTS * TOTAL_PNG_SIZE, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
  shmid_positions = shmget(IPC_PRIVATE, sizeof(int) * 50, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);

  if (shmid_seg_data == -1
    || shmid_seg_size == -1
    || shmid_spaces == -1
    || shmid_items == -1
    || shmid_produce_count == -1
    || shmid_consume_count == -1
    || shmid_status == -1
    || shmid_mutex == -1
	  || shmid_segments == -1
	) {
		if(shmid_segments == -1){
			fprintf(stderr, "error: segments cant be made\n");
		}
    perror("shmget");
    abort();
  }

  if (shared_memory_attach() != 0) {
    perror("shmat");
    abort();
  }
 
  int ret = 0;

	if (sem_init(spaces, 1, B) != 0) {
    fprintf(stderr, "error: failed to create semaphore\n");
    ret = -1;
  }

  if (sem_init(items, 1, 0) != 0) {
    fprintf(stderr, "error: failed to create semaphore\n");
    ret = -1;
  }

  pthread_mutexattr_t attr;

  if (pthread_mutexattr_init(&attr) != 0) {
    fprintf(stderr, "error: failed to create mutex attr\n");
    ret = -1;
  }

  if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0) {
    fprintf(stderr, "error: failed to set pshared on mutex attr\n");
    ret = -1;
  }

  if (pthread_mutex_init(mutex, &attr) != 0) {
    fprintf(stderr, "error: failed to create mutex\n");
    ret = -1;
  }

  pthread_mutexattr_destroy(&attr);
  
  curl_global_init(CURL_GLOBAL_DEFAULT);
  
  struct timeval t0;
  gettimeofday(&t0, 0);

  bool parent = true;
  int Ppids[P];
  int Cpids[C];
  int pid = 0;
  int state;
	
  if (ret == 0) {
    for (i = 0; i < P; i++) {
      pid = producer(i);
      if (pid > 0) {        /* parent proc */
        Ppids[i] = pid;
      } else if (pid == 0) { /* child proc */
        parent = false;
        break;
      } else {
        ret = -1;
        fprintf(stderr, "error: failed to fork producer\n");
      }
    }
  }
	
	if (parent && ret == 0) {
		for (i = 0; i < C; i++) {
			pid = consumer(i);
			if (pid > 0) {        /* parent proc */
				Cpids[i] = pid;
			} else if (pid == 0) { /* child proc */
      	parent = false;
				break;
			} else {
				ret = -1;
				fprintf(stderr, "error: failed to fork consumer\n");
			}
		}
	
    if (ret == 0 && parent && *status == 0) { /* Parent process */ 
      for (i = 0; i < P; i++) {
        waitpid(Ppids[i], &state, 0);
        if (!WIFEXITED(state)) {
          fprintf(stderr, "error: producer #%d with pid=%d has and error or has gotten stuck.", i, Ppids[i]);
          ret = -1;
          break;
        }
      }
      for (i = 0; i < C; i++) {
        waitpid(Cpids[i], &state, 0);
        if (!WIFEXITED(state)) {
          fprintf(stderr, "error: consumer #%d with pid=%d has and error or has gotten stuck.", i, Cpids[i]);
          ret = -1;
          break;
        }
      }
      parent = true;
      if (*status == 0) {
        struct PNG out;
        memcpy(&out.IHDR, &segments[0].IHDR, DATA_IHDR_SIZE);
   
        for (i = 1; i < NUM_IMAGE_SEGMENTS; i++) {
          if (segments[i].IHDR.width != out.IHDR.width) {
            fprintf(stderr, "error: invalid response, images width's are inconsistent\n");
            *status = -1;
            break;
          }
          if (segments[i].idat_length > 1) {
            fprintf(stderr, "error: invalid response, expected single IDAT PNG\n");
            *status = -1;
            break;
          }
          out.IHDR.height += segments[i].IHDR.height;
        }
        if (*status == 0) {
          out.p_IDAT = malloc(sizeof(struct chunk));
          out.idat_length = 1;
          out.p_IDAT->length = (1 + 4 * out.IHDR.width) * out.IHDR.height;
          out.p_IDAT->p_data = malloc(out.p_IDAT->length);
          memcpy(out.p_IDAT->type, "IDAT", 4);
          U8* ptr = out.p_IDAT->p_data;
          int decomptotal = 0;
          U64 decompressed_length;
          for (i = 0; i < NUM_IMAGE_SEGMENTS; i++) {
            if (mem_inf(ptr, &decompressed_length, segments[i].p_IDAT->p_data, segments[i].p_IDAT->length) == 0) {
              ptr += decompressed_length;
              decomptotal += decompressed_length;
            } else {
              fprintf(stderr, "error: failed to decompress PNG data for image: %d\n", i);
              *status = -1;
              break;
            }
          }
          if (*status == 0) {
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
                *status = -1;
              }
            } else {
              fprintf(stderr, "error: failed to compress concatenated image\n");
              *status = -1;
              free(data);
            }
          }
          free_png_data(&out);
        }
      }

      struct timeval t1;
      gettimeofday(&t1, 0);
      printf("paster2 execution time: %f seconds\n", (float) (t1.tv_sec - t0.tv_sec) + (float) (t1.tv_usec - t0.tv_usec) / 1000000.0f);
     
    } else {
      ret = *status;
    }
  }

  if (parent) {
    sem_destroy(spaces);
    sem_destroy(items);
  }

  shmdt(segment_data);
  shmdt(segment_size);
  shmdt(spaces);
  shmdt(items);
  shmdt(produce_count);
  shmdt(consume_count);
  shmdt(status);
  shmdt(mutex);
  shmdt(segments);
  shmdt(positions);

  if (parent) {
    shmctl(shmid_seg_data, IPC_RMID, NULL);
    shmctl(shmid_seg_size, IPC_RMID, NULL);
    shmctl(shmid_spaces, IPC_RMID, NULL);
    shmctl(shmid_items, IPC_RMID, NULL);
    shmctl(shmid_produce_count, IPC_RMID, NULL);
    shmctl(shmid_consume_count, IPC_RMID, NULL);
    shmctl(shmid_status, IPC_RMID, NULL);
    shmctl(shmid_mutex, IPC_RMID, NULL);
	  shmctl(shmid_segments, IPC_RMID, NULL);
	  shmctl(shmid_positions, IPC_RMID, NULL);
  }

  curl_global_cleanup();
	
  return ret;
}
