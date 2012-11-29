#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>      /* va_arg, va_start() */
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <math.h>        /* ceilf() */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>     /* stat() */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <argp.h>
#include "memory.h"

#define MAX_IDLE_TIME 60
#define MAX_WORKERS 120
#define BUFFER_SIZE 256
#define ADP_BUF_SIZE 64
#define TIMEOUT_SECS 3
#define MAX_EVENTS 100
#define CLI_STOR_INCR 40
#define HIGH_PRI_PCT .2
#define MED_PRI_PCT .3
/* LOW_PRI_PCT .5 */

struct _threadpool;
typedef struct _threadpool threadpool_t;

struct _threadpool_task;
typedef struct _threadpool_task threadpool_task_t;

struct _threadpool {
  int count;
  int workers;
  threadpool_task_t *cached;
  threadpool_task_t *stopped;
  pthread_mutex_t lock;
  pthread_cond_t notify;
  int shutdown;
  int running;
};

struct _threadpool_task {
  pthread_t tid;
  int id;
  int cid;
  char addr[INET6_ADDRSTRLEN];
  int socketfd;
  int cached;
  int stopped;
  pthread_mutex_t lock;  /* Mostly for notification purposes */
  pthread_cond_t notify; /* Threads should not conflict here */
  threadpool_task_t *prev; /* These fields are protected by  */
  threadpool_task_t *next; /* the main pool lock not t->lock */
};

typedef struct _clientinfo {
  threadpool_task_t *parent;
  char buffer[BUFFER_SIZE];
  size_t name_len;
  char *filename;
  FILE *file;
  int filefd;
  struct stat st;
  size_t remain;
  off_t offset;
} clientinfo;

typedef struct _clientpri {
  int cid;
  int speed;
} clientpri;

typedef struct _prioritylocks {
  clientpri *clients;
  int num_clients;
  int capacity;
  pthread_mutex_t lock;
  int high_t;
  int med_t;
  int released;
  int next; /* 0 high, 1 med, 2 low */
  int current;
  int high_c;
  pthread_mutex_t high_l;
  pthread_cond_t high_n;
  int med_c;
  pthread_mutex_t med_l;
  pthread_cond_t med_n;
  int low_c;
  pthread_mutex_t low_l;
  pthread_cond_t low_n;
} prioritylocks;

typedef struct _cli_evt {
  int cid;
  int fd;
} cli_evt;

typedef struct _adp_evt_list {
  cli_evt *event;
  struct _adp_evt_list *next;
} adp_evt_list;

typedef struct _adaptive_events {
  pthread_t tid;
  adp_evt_list *events;
  adp_evt_list *tail;
  pthread_mutex_t lock;
  pthread_cond_t notify;
} adaptive_events;

int executor_init();
int executor_execute(int socketfd, int cid, char *addr);
void executor_shutdown();
void executor_thread_shutdown(threadpool_task_t *t);
void executor_thread_done(threadpool_task_t *t);
int executor_thread_expire(threadpool_task_t *t);
void executor_garbage_collect();
void *executor_thread(void *task);

#endif
