#include "server.h"

/**
 Globals
*/
const char *program_name;
static const char *image_dir;
static int sockfd;
static int verbose_f;

static int adaptive_f;
static pthread_t adaptive_tid;
static int atid_v;
static int adaptivefd;
static int adaptiveport;
prioritylocks adaptive_d;

static void global_exit(int status);
static void expectMe();
static void unexpectMe();
static void scheduleMe(int cid);

/**
 Misc. Helper Functions
*/
static void verbose(const char *format, ...) {
  if (!verbose_f) return;
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
  va_end(args);
}

#ifndef DIR_SEPARATOR
#define DIR_SEPARATOR '/'
#endif

#if defined (_WIN32) || defined (__MSDOS__) || defined (__DJGPP__) || \
  defined (__OS2__)
#define HAVE_DOS_BASED_FILE_SYSTEM
#ifndef DIR_SEPARATOR_2 
#define DIR_SEPARATOR_2 '\\'
#endif
#endif

/* Define IS_DIR_SEPARATOR.  */
#ifndef DIR_SEPARATOR_2
# define IS_DIR_SEPARATOR(ch) ((ch) == DIR_SEPARATOR)
#else /* DIR_SEPARATOR_2 */
# define IS_DIR_SEPARATOR(ch) \
	(((ch) == DIR_SEPARATOR) || ((ch) == DIR_SEPARATOR_2))
#endif /* DIR_SEPARATOR_2 */
char *basename (const char *name) {
  const char *base;
  for (base = name; *name; name++) {
    if (IS_DIR_SEPARATOR (*name)) {
	    base = name + 1;
	  }
  }
  return (char *) base;
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int get_port_num(int sock_fd) {
  struct sockaddr_storage sock_addr;
  struct sockaddr *sa;
  socklen_t salen;
  sa = (struct sockaddr *)&sock_addr;
  salen = sizeof(sock_addr);
  getsockname(sock_fd, (struct sockaddr *)&sock_addr, &salen);
  if (sa->sa_family == AF_INET) {
    return ntohs(((struct sockaddr_in *)sa)->sin_port);
  }
  return ntohs(((struct sockaddr_in6 *)sa)->sin6_port);
}

int create_and_bind_sock(int port) {
  int yes=1, portlen, r, newfd;
  struct addrinfo hints, *servinfo, *p;
  char *portstr;
  
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;  // use IPv4 or IPv6, whichever
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  
  portlen = snprintf(NULL, 0, "%d", port);
  portstr = (char *)emalloc(portlen+1);
  snprintf(portstr, portlen+1, "%d", port);
  if ((r = getaddrinfo(NULL, portstr, &hints, &servinfo)) != 0){
    fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(r));
  }
  efree(portstr);
  
  // loop through all the results and bind to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((newfd = socket(p->ai_family, p->ai_socktype,
          p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }
    
    if (setsockopt(newfd, SOL_SOCKET, SO_REUSEADDR, &yes,
          sizeof(int)) == -1) {
      perror("setsockopt");
      global_exit(1);
    }

    if (bind(newfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(newfd);
      perror("bind");
      continue;
    }

    break;
  }
  if (p == NULL) newfd = -1;
  freeaddrinfo(servinfo);
  return newfd;
}

static size_t trim_in_place(char *string) {
  int start, end;
  size_t len;
  char *buffer;
  char *p = string;
  start = 0;
  while (isspace(*p)) {
    ++start;
    ++p;
  }
  if (*p == '\0') { // All spaces?
    *string = '\0'; // Empty string that shit
    return 0;
  }
  end = strlen(string) - 1;
  p = string + end;
  while (p > string && isspace(*p)) {
    --end;
    --p;
  }
  ++end; /* we went too far */
  len = end-start;
  buffer = (char *)emalloc(len+1); /* room for null char */
  memcpy(buffer, (string + start), len);
  buffer[len] = '\0';
  memcpy(string, buffer, len+1);
  efree(buffer);
  return len;
}

static int contains_parent_path(char *path){
  int len = strlen(path);
  if (len == 2 && strcmp(path, "..") == 0)
    return 1;
  if (len > 2) {
    if (strncmp(path, "../", 3) == 0)
      return 1;
    if (strcmp((path+strlen(path)-3), "/..") == 0)
      return 1;
    if (strstr(path, "/../") != NULL)
      return 1;
  }
  return 0;
}

/**
 Executor Cached Thread Pool
*/
threadpool_t pool;

int executor_init() {
  pool.count = 0;
  pool.workers = 0;
  pool.cached = NULL;
  pool.shutdown = 0;
  pool.running = 1;
  return pthread_mutex_init(&(pool.lock), NULL);
}

int executor_execute(int socketfd, int cid, char *addr) {
  int r;
  pthread_mutex_lock(&(pool.lock));
  if (!pool.running) {
    pthread_mutex_unlock(&(pool.lock));
    errno = EINTR;
    return -1;
  }
  threadpool_task_t *t;
  if (pool.cached == NULL) { /* No cached threads */
    if (pool.workers >= MAX_WORKERS) {
      pthread_mutex_unlock(&(pool.lock));
      errno = EBUSY;
      return -1;
    }
    t = ALLOC(threadpool_task_t);
    t->id = ++pool.count;
    t->cid = cid;
    strncpy(t->addr, addr, INET6_ADDRSTRLEN);
    t->socketfd = socketfd;
    t->cached = 0;
    t->stopped = 0;
    pthread_mutex_init(&(t->lock), NULL);
    pthread_cond_init(&(t->notify), NULL);
    t->prev = NULL;
    t->next = NULL;
    r = pthread_create(&(t->tid), NULL, executor_thread, t);
    if (r) {
      errno = r;
      perror("pthread_create");
    } else {
      ++pool.workers;
    }
  } else { /* Use a cached thread */
    t = pool.cached;
    pthread_mutex_lock(&(t->lock));
    if (t->cached != 1){
      fprintf(stderr, "Executor: Fatal Error: Cached thread-%d is not cached\n", t->id);
      global_exit(1);
    }
    if (t->stopped){
      fprintf(stderr, "Executor: Fatal Error: Cached thread-%d has terminated\n", t->id);
      global_exit(1);
    }
    
    /* Pop off cached pool */
    pool.cached = t->next;
    if (pool.cached)
      pool.cached->prev = NULL;
    t->next = NULL;
    
    /* Init */
    t->cid = cid;
    strncpy(t->addr, addr, INET6_ADDRSTRLEN);
    t->socketfd = socketfd;
    t->cached = 0;
    
    pthread_cond_signal(&(t->notify));
    pthread_mutex_unlock(&(t->lock));
  }
  r = t->id;
  executor_garbage_collect();
  pthread_mutex_unlock(&(pool.lock));
  return r;
}

void executor_shutdown() {
  pthread_mutex_lock(&(pool.lock));
  /* Signal all threads to shutdown */
  pool.shutdown = 1;
  pool.running = 0;
  
  threadpool_task_t *t, *s;
  t = pool.cached;
  while (t) {
    pthread_mutex_lock(&(t->lock));
    pthread_cond_signal(&(t->notify));
    pthread_mutex_unlock(&(t->lock));
    s = t;
    t = t->next;
  }
  pool.cached = NULL;
  while (pool.workers > 0) {
    pthread_cond_wait(&(pool.notify), &(pool.lock));
  }
  executor_garbage_collect();
  pthread_mutex_unlock(&(pool.lock));
}

void executor_thread_shutdown(threadpool_task_t *t) {
  pthread_mutex_lock(&(pool.lock));
  t->next = pool.stopped;
  if (pool.stopped)
    pool.stopped->prev = t;
  pool.stopped = t;
  --pool.workers;
  pthread_cond_signal(&(pool.notify));
  pthread_mutex_unlock(&(pool.lock));
}

void executor_thread_done(threadpool_task_t *t) {
  pthread_mutex_lock(&(pool.lock));
  t->next = pool.cached;
  if (pool.cached)
    pool.cached->prev = t;
  pool.cached = t;
  t->socketfd = -1;
  t->cached = 1;
  pthread_mutex_unlock(&(pool.lock));
}

int executor_thread_expire(threadpool_task_t *t) {
  if(pthread_mutex_trylock(&(pool.lock))) {
    /* Did not get lock, main thread possibly in a executor_execute or shutdown */
    fprintf(stderr, "executor_thread_expire: Warning: Could not acquire lock.\n");
    return 1;
  }
  t->stopped = 1;
  
  if (t->prev)
    t->prev->next = t->next;
  if (t->next)
    t->next->prev = t->prev;
  if (t == pool.cached)
    pool.cached = t->next;
  
  t->next = pool.stopped;
  if (pool.stopped)
    pool.stopped->prev = t;
  pool.stopped = t;
  --pool.workers;
  pthread_mutex_unlock(&(pool.lock));
  return 0;
}

void executor_garbage_collect() {
  int cleaned = 0;
  /* Expects caller to hold a lock to pool.lock */
  if (pool.stopped) {
    threadpool_task_t *t, *s;
    t = pool.stopped;
    while (t) {
      pthread_join(t->tid, NULL);
      s = t;
      t = t->next;
      efree(s);
      cleaned++;
    }
    pool.stopped = NULL;
    verbose("Executor: Garbage collect freed %d threads.", cleaned);
  }
}

static void handle_cleanup(void *arg) {
  clientinfo *ci = (clientinfo *)arg;
  if (adaptive_f)
      unexpectMe();
  if (ci->parent->socketfd != -1)
    close(ci->parent->socketfd);
  if (ci->filename)
    efree(ci->filename);
  efree(ci);
}

void *executor_thread(void *task) {
  threadpool_task_t *t = (threadpool_task_t *)task;
  int r; /* Return values from system calls */
  char *msg;
  char send_buf[BUFFER_SIZE];
  clientinfo *ci = NULL;
  for (;;) {
    if (pool.shutdown) {
      if (t->socketfd != -1)
        close(t->socketfd);
      executor_thread_shutdown(t);
      pthread_exit(NULL);
      return NULL;
    }
    ci = ALLOC(clientinfo);
    /* Initialize client variables */
    ci->parent = t;
    ci->filefd = -1;
    ci->name_len = 0;
    ci->remain = 0;
    ci->offset = 0;
    ci->filename = NULL;
    /* DO WORK */
    verbose("Thread-%d: Got client %d at %s.", t->id, t->cid, t->addr);
    if (adaptive_f)
      snprintf(send_buf, BUFFER_SIZE, "HELLO:%d:%d\n",t->cid,adaptiveport);
    else
      snprintf(send_buf, BUFFER_SIZE, "HELLO:%d\n",t->cid);
    r = send(t->socketfd, send_buf, strlen(send_buf),0);
    if (r == -1) {
      verbose("Thread-%d: send1: %s", t->id, strerror(errno));
      close(t->socketfd);
      executor_thread_expire(t);
      pthread_exit(NULL);
      return NULL;
    }
    pthread_cleanup_push(handle_cleanup, (void *)ci);
    if (adaptive_f)
      expectMe(); /* Don't prematurely fire next wave */
    for (;;) {
      if (pool.shutdown) {
        executor_thread_shutdown(t);
        pthread_exit(NULL);
        return NULL;
      }
      if (adaptive_f) {
        /* Wait yo turn */
        scheduleMe(t->cid);
      }
      /* Get client input */
      memset(ci->buffer, 0, BUFFER_SIZE);
      r = recv(t->socketfd, ci->buffer, BUFFER_SIZE-1, 0);
      if (r == -1) {
        verbose("Thread-%d: recv: error: %s", t->id, strerror(errno));
        break;
      } else if (r == 0) {
        verbose("Thread-%d: Client %d disconnected.", t->id, t->cid);
        if (!verbose_f)
          fprintf(stderr, "[%s] Disconnect\n", t->addr);
        break;
      }
      /* else got data */
      ci->buffer[r] = '\0';
      if (ci->buffer[r-1] != '\n') {
        snprintf(send_buf, BUFFER_SIZE, "ERROR:Internal Server Error\n");
        verbose("Thread-%d: Illegal or corrupted client command", t->id);
        r = send(t->socketfd, send_buf, strlen(send_buf),0);
        if (r == -1) {
          verbose("Thread-%d: send(2): %s", t->id, strerror(errno));
          executor_thread_expire(t);
          pthread_exit(NULL);
          return NULL;
        }
        continue;
      }
      trim_in_place(ci->buffer);
      verbose("Thread-%d: Got input \"%s\" from client.", t->id, ci->buffer);
      if (contains_parent_path(ci->buffer)) {
        snprintf(send_buf, BUFFER_SIZE, "ERROR:Illegal path\n");
        verbose("Thread-%d: Parent path forbidden.", t->id);
        r = send(t->socketfd, send_buf, strlen(send_buf),0);
        if (r == -1) {
          verbose("Thread-%d: send(3): %s", t->id, strerror(errno));
          executor_thread_expire(t);
          pthread_exit(NULL);
          return NULL;
        }
        continue;
      }
      ci->name_len = strlen(image_dir) + strlen(ci->buffer)+1;
      ci->filename = (char *)emalloc(ci->name_len);
      snprintf(ci->filename, ci->name_len, "%s%s", image_dir, ci->buffer);
      verbose("Thread-%d: Attempting to open file \"%s\"", t->id, ci->filename);
      ci->file = fopen(ci->filename, "rb");
      efree(ci->filename);
      ci->filename = NULL;
      if (!ci->file) {
        msg = strerror(errno);
        snprintf(send_buf, BUFFER_SIZE, "ERROR:%s\n", msg);
        verbose("Thread-%d: fopen: %s", t->id, msg);
        r = send(t->socketfd, send_buf, strlen(send_buf),0);
        if (r == -1) {
          verbose("Thread-%d: send(4): %s", t->id, strerror(errno));
          executor_thread_expire(t);
          pthread_exit(NULL);
          return NULL;
        }
      } else {
        verbose("Thread-%d: Found file. Transmitting to client.", t->id);
        ci->filefd = fileno(ci->file);
        fstat(ci->filefd, &ci->st);
        if (S_ISDIR (ci->st.st_mode)) {
          snprintf(send_buf, BUFFER_SIZE, "ERROR:File is a directory\n");
          verbose("Thread-%d: File is a directory.", t->id);
          r = send(t->socketfd, send_buf, strlen(send_buf),0);
          if (r == -1) {
            verbose("Thread-%d: send(5): %s", t->id, strerror(errno));
            executor_thread_expire(t);
            pthread_exit(NULL);
            return NULL;
          }
          continue;
        }
        snprintf(send_buf, BUFFER_SIZE, "FILE:%ld\n", (long)ci->st.st_size);
        r = send(t->socketfd, send_buf, strlen(send_buf),0);
        if (r == -1) {
          verbose("Thread-%d: send(6): %s", t->id, strerror(errno));
          executor_thread_expire(t);
          pthread_exit(NULL);
          return NULL;
        }
        ci->remain = ci->st.st_size;
        ci->offset = 0;
        ssize_t sent;
        while (ci->remain > 0) {
          sent = sendfile(t->socketfd, ci->filefd, &ci->offset, ci->remain);
          if (sent == -1) {
            verbose("Thread-%d: sendfile: %s", t->id, strerror(errno));
            break;
          } else if (sent == 0) {
            fprintf(stderr,"sendfile returned 0? aborting send.\n");
            break;
          } else if (sent != (ssize_t) ci->remain) {
            ci->remain -= sent;
            verbose("Thread-%d: Partial transmission of %ld bytes, retrying %ld from %ld.", t->id, (long)sent, (long)ci->remain, (long)ci->offset);
          } else {
            break;
          }
        }
        fclose(ci->file);
      }
    }
    if (adaptive_f)
      unexpectMe();
    close(t->socketfd);
    efree(ci);
    pthread_cleanup_pop(0);
    pthread_mutex_lock(&(t->lock));
    executor_thread_done(t);
    while (t->cached && !pool.shutdown) {
      struct timespec to;
      memset(&to, 0, sizeof to);
      to.tv_sec = time(0) + MAX_IDLE_TIME;
      to.tv_nsec = 0;
      verbose("Thread-%d: Waiting for next job.", t->id);
      r = pthread_cond_timedwait(&(t->notify), &(t->lock), &to);
      if (r == ETIMEDOUT) {
        verbose("Thread-%d: Idle timeout, marking self for removal.", t->id);
        if (executor_thread_expire(t)) {
          continue;
        }
        pthread_mutex_unlock(&(t->lock));
        pthread_exit(NULL);
        return NULL;
      }
    }
    pthread_mutex_unlock(&(t->lock));
  }
  pthread_exit(NULL);
  return NULL;
}

/**
  Adaptive Scheduler Service
*/
static void initialize_adaptive() {
  adaptive_d.clients = NULL;
  adaptive_d.num_clients = 0;
  adaptive_d.capacity = 0;
  adaptive_d.released = 0;
  adaptive_d.returned = 0;
  adaptive_d.next = 0;
  adaptive_d.high_c = 0;
  adaptive_d.med_c = 0;
  adaptive_d.low_c = 0;
  pthread_mutex_init(&(adaptive_d.lock), NULL);
  pthread_mutex_init(&(adaptive_d.high_l), NULL);
  pthread_cond_init(&(adaptive_d.high_n), NULL);
  pthread_mutex_init(&(adaptive_d.med_l), NULL);
  pthread_cond_init(&(adaptive_d.med_n), NULL);
  pthread_mutex_init(&(adaptive_d.low_l), NULL);
  pthread_cond_init(&(adaptive_d.low_n), NULL);
}

/* BEGIN NEED adaptive_d.lock */
static int getClientpriIndById(int cid) {
  if (adaptive_d.num_clients == 0)
    return -1;
  int i;
  for (i = 0; i < adaptive_d.num_clients; i++) {
    if (adaptive_d.clients[i].cid == cid)
      return i;
  }
  return -1;
}

static void removeClientpri(int cid) {
  int i = getClientpriIndById(cid);
  if (i == -1) {
    verbose("Warning: Attempted removal of non-existant client.");
    return;
  }
  if (i < adaptive_d.num_clients - 1) {
    /* [ x x i x x ] => [ x x x x ] */
    memmove(&adaptive_d.clients[i], &adaptive_d.clients[i+1], (adaptive_d.num_clients-i-1)*sizeof(clientpri));
  } /* else tail of list */
  --adaptive_d.num_clients;
}

static void updateClientpri(int cid, int speed) {
  clientpri *t;
  int i, s, clientInd;
  clientInd = getClientpriIndById(cid);
  speed = speed < 1 ? 1 : speed; /* Lower bound */
  i = clientInd;
  if (clientInd != -1) {
    s = adaptive_d.clients[i].speed;
    adaptive_d.clients[i].speed = speed;
    if (speed > s && i < adaptive_d.num_clients - 1) {
      do {
        ++i;
      } while (i < adaptive_d.num_clients && adaptive_d.clients[i].speed <= speed);
      /* <= for FCFS ordering, new client of same speed is ordered after existing */
      --i;
      if (i != clientInd) {
        t = ALLOC(clientpri);
        memcpy(t, &adaptive_d.clients[clientInd], sizeof(clientpri));
        memmove(&adaptive_d.clients[clientInd], &adaptive_d.clients[clientInd+1], (i-clientInd)*sizeof(clientpri));
        memcpy(&adaptive_d.clients[i], t, sizeof(clientpri));
        efree(t);
      }
    } else if (speed < s && i > 0) {
      do {
        --i;
      } while (i >= 0 && adaptive_d.clients[i].speed > speed);
      ++i;
      if (i != clientInd) {
        t = ALLOC(clientpri);
        memcpy(t, &adaptive_d.clients[clientInd], sizeof(clientpri));
        memmove(&adaptive_d.clients[i+1], &adaptive_d.clients[i], (clientInd-i)*sizeof(clientpri));
        memcpy(&adaptive_d.clients[i], t, sizeof(clientpri));
        efree(t);
      }
    }
  } else {
    if (adaptive_d.capacity < adaptive_d.num_clients + 1) {
      adaptive_d.clients = (clientpri *)erealloc(adaptive_d.clients, (adaptive_d.capacity + CLI_STOR_INCR)*sizeof(clientpri));
      adaptive_d.capacity += CLI_STOR_INCR;
    }
    /* i = -1 */
    do {
      ++i;
    } while (i < adaptive_d.num_clients && adaptive_d.clients[i].speed <= speed);
    if (i != adaptive_d.num_clients)
      memmove(&adaptive_d.clients[i+1], &adaptive_d.clients[i], (adaptive_d.num_clients - i)*sizeof(clientpri));
    /* memset(&adaptive_d.clients[i], 0, sizeof(clientpri)); */
    adaptive_d.clients[i].cid = cid;
    adaptive_d.clients[i].speed = speed;
    ++adaptive_d.num_clients;
  }
}

static void updateCutoffs() {
  float c;
  c = (float)adaptive_d.num_clients;
  adaptive_d.high_t = (int)ceilf(c * HIGH_PRI_PCT);
  adaptive_d.med_t = adaptive_d.high_t + (int)ceilf(c * MED_PRI_PCT);
}

static int sched_checkrelease() {
  int i, n, release = -1;
  if (adaptive_d.returned == adaptive_d.released) {
    /* Oh boy everyone is back lets let the next wave go */
    for (i = 0; i < 3; ++i) {
      switch(adaptive_d.next) {
        case 0:
          n = adaptive_d.high_c;
          release = 0;
          adaptive_d.high_c = 0;
          adaptive_d.current = 0;
          break;
        case 1:
          n = adaptive_d.med_c;
          release = 1;
          adaptive_d.med_c = 0;
          adaptive_d.current = 1;
          break;
        case 2:
          n = adaptive_d.low_c;
          release = 2;
          adaptive_d.low_c = 0;
          adaptive_d.current = 2;
          break;
        default:
          break;
      }
      ++adaptive_d.next;
      if (adaptive_d.next > 2) adaptive_d.next = 0;
      if (n) {
        adaptive_d.released = n;
        adaptive_d.returned = 0;
        break;
      } /* else if n = 0 then nobody in that priority class is waiting, go to next one */
    }
  }
  return release;
}
/* END need adaptive_d.lock */

static void expectMe() {
  pthread_mutex_lock(&(adaptive_d.lock));
  ++adaptive_d.released;
  pthread_mutex_unlock(&(adaptive_d.lock));
}

static void unexpectMe() {
  /* I won't be back */
  int release;
  pthread_mutex_lock(&(adaptive_d.lock));
  --adaptive_d.released;
  release = sched_checkrelease();
  pthread_mutex_unlock(&(adaptive_d.lock));
  switch (release) {
    case 0:
      pthread_mutex_lock(&(adaptive_d.high_l));
      pthread_cond_broadcast(&(adaptive_d.high_n));
      pthread_mutex_unlock(&(adaptive_d.high_l));
      break;
    case 1:
      pthread_mutex_lock(&(adaptive_d.med_l));
      pthread_cond_broadcast(&(adaptive_d.med_n));
      pthread_mutex_unlock(&(adaptive_d.med_l));
      break;
    case 2:
      pthread_mutex_lock(&(adaptive_d.low_l));
      pthread_cond_broadcast(&(adaptive_d.low_n));
      pthread_mutex_unlock(&(adaptive_d.low_l));
      break;
    default:
      break;
  }
}

static void scheduleMe(int cid) {
  int i, which, release;
  pthread_mutex_lock(&(adaptive_d.lock));
  i = getClientpriIndById(cid);
  if (i == -1)
    i = adaptive_d.num_clients; /* lowest priority for clients who have not checked in with adaptive server */
  ++adaptive_d.returned; /* Check in */
  
  /* Sorting hat */
  if (i < adaptive_d.high_t) {
    which = 0;
  } else if (i < adaptive_d.med_t) {
    which = 1;
  } else {
    which = 2;
  }
  switch (which) {
    case 0:
      ++adaptive_d.high_c;
      break;
    case 1:
      ++adaptive_d.med_c;
      break;
    case 2:
      ++adaptive_d.low_c;
      break;
    default:
      break;
  }
  
  /* Are we all here? */
  release = sched_checkrelease();
  pthread_mutex_unlock(&(adaptive_d.lock));
  
  /* Release next group */
  switch (release) {
    case 0:
      verbose("Adaptive: Releasing high priority clients");
      pthread_mutex_lock(&(adaptive_d.high_l));
      pthread_cond_broadcast(&(adaptive_d.high_n));
      pthread_mutex_unlock(&(adaptive_d.high_l));
      break;
    case 1:
      verbose("Adaptive: Releasing med priority clients");
      pthread_mutex_lock(&(adaptive_d.med_l));
      pthread_cond_broadcast(&(adaptive_d.med_n));
      pthread_mutex_unlock(&(adaptive_d.med_l));
      break;
    case 2:
      verbose("Adaptive: Releasing low priority clients");
      pthread_mutex_lock(&(adaptive_d.low_l));
      pthread_cond_broadcast(&(adaptive_d.low_n));
      pthread_mutex_unlock(&(adaptive_d.low_l));
      break;
    default:
      break;
  }
  /* Wait for my next turn, or release self if part of next group */
  if (which != release) {
    while (which != adaptive_d.current) {
      switch (which) {
        case 0:
          pthread_mutex_lock(&(adaptive_d.high_l));
          pthread_cond_wait(&(adaptive_d.high_n), &(adaptive_d.high_l));
          pthread_mutex_unlock(&(adaptive_d.high_l));
          break;
        case 1:
          pthread_mutex_lock(&(adaptive_d.med_l));
          pthread_cond_wait(&(adaptive_d.med_n), &(adaptive_d.med_l));
          pthread_mutex_unlock(&(adaptive_d.med_l));
          break;
        case 2:
          pthread_mutex_lock(&(adaptive_d.low_l));
          pthread_cond_wait(&(adaptive_d.low_n), &(adaptive_d.low_l));
          pthread_mutex_unlock(&(adaptive_d.low_l));
          break;
        default:
          break;
      }
    }
  }
}

static void shutdown_adaptive(void *arg) {
  verbose("Adaptive: Shutting down scheduler...");
  int epollfd = (int)(*(int *)arg);
  close(epollfd);
}

void *adaptive_scheduler(void *arg) {
  socklen_t clilen;
  struct sockaddr_storage cli_addr;
  struct epoll_event ev, events[MAX_EVENTS];
  struct timeval tv;
  int cfd, epollfd, nfds, n, r, cid;
  char buffer[ADP_BUF_SIZE];
  clilen = sizeof(cli_addr);
  tv.tv_sec = TIMEOUT_SECS;
  tv.tv_usec = 0;
  
  pthread_cleanup_push(shutdown_adaptive, (void *)&epollfd);
  if ((epollfd = epoll_create(10)) == -1) {
    perror("epoll_create");
    pthread_exit(NULL);
    return NULL;
  }
  
  ev.events = EPOLLIN;
  ev.data.fd = adaptivefd;
  if (epoll_ctl(epollfd, EPOLL_CTL_ADD, adaptivefd, &ev) == -1) {
    perror("epoll_ctl: listener");
    pthread_exit(NULL);
    return NULL;
  }
  
  for (;;) {
    if ((nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1)) == -1) {
      perror("epoll_wait");
      pthread_exit(NULL);
      return NULL;
    }
    for (n = 0; n < nfds; ++n) {
      if (events[n].data.fd == adaptivefd) {
        if ((cfd = accept(adaptivefd, (struct sockaddr *)&cli_addr, &clilen)) < 0) {
          perror("accept");
          close(adaptivefd);
          pthread_exit(NULL);
          return NULL;
        }
        if (setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,  sizeof tv)) {
          perror("setsockopt");
          pthread_exit(NULL);
          return NULL;
        }
        
        r = recv(cfd, buffer, ADP_BUF_SIZE-1, 0);
        if (r == -1) {
          verbose("Adaptive: recv: %s. Discarding client", strerror(errno));
          close(cfd);
          continue;
        } else if (r == 0) {
          verbose("Adaptive: Client disconnect before sending data");
          close(cfd);
          continue;
        }
        buffer[r] = '\0';
        if (buffer[r-1] != '\n'){
          verbose("Adaptive: Client handshake failed. Discarding client");
          close(cfd);
          continue;
        }
        trim_in_place(buffer);
        errno = 0;
        cid = (int)strtol(buffer,NULL,0);
        if (errno == ERANGE) { /* Not a number? or number too big? */
          verbose("Adaptive: Invalid client handshake. Aborting.");
          continue;
        }
        r = send(cfd, "OK\n", 3, 0);
        if (r == -1) {
          verbose("Adaptive: Client handshake failed on response. Aborting.");
          close(cfd);
          continue;
        }
        pthread_mutex_lock(&(adaptive_d.lock));
        updateClientpri(cid, 1);
        updateCutoffs();
        r = getClientpriIndById(cid);
        adaptive_d.clients[r].fd = cfd;
        pthread_mutex_unlock(&(adaptive_d.lock));
        
        ev.events = EPOLLIN;
        ev.data.ptr = (void *)&adaptive_d.clients[r];
        if (epoll_ctl(epollfd, EPOLL_CTL_ADD, cfd, &ev) == -1) {
          perror("epoll_ctl: client");
          pthread_exit(NULL);
          return NULL;
        }
      } else {
        cid = ((clientpri *)events[n].data.ptr)->cid;
        cfd = ((clientpri *)events[n].data.ptr)->fd;
        r = recv(cfd, buffer, ADP_BUF_SIZE-1, 0);
        if (r <= 0) {
          /* Client disconnect */
          verbose("Adaptive: Client %d disconnected.", cid);
          close(cfd);
          pthread_mutex_lock(&(adaptive_d.lock));
          removeClientpri(cid);
          updateCutoffs();
          pthread_mutex_unlock(&(adaptive_d.lock));
          continue;
        }
        buffer[r] = '\0';
        if (buffer[r-1] != '\n') {
          verbose("Adaptive: Malformed speed update from client %d.", cid);
          continue;
        }
        trim_in_place(buffer);
        verbose("Adaptive: Got update from client %d: %s.", cid, buffer);
        errno = 0;
        r = (int)strtol(buffer,NULL,0);
        if (errno == ERANGE) { /* Not a number? or number too big? */
          verbose("Adaptive: Invalid speed update from client %d.", cid);
          continue;
        }
        pthread_mutex_lock(&(adaptive_d.lock));
        updateClientpri(cid, r);
        pthread_mutex_unlock(&(adaptive_d.lock));
      }
    }
  }
  pthread_cleanup_pop(0);
  pthread_exit(NULL);
}

/**
  Main
*/
static void global_exit(int status) {
  if (sockfd != -1)
    close(sockfd);
  if (adaptivefd != -1)
    close(adaptivefd);
  if (atid_v) {
    pthread_cancel(adaptive_tid);
    pthread_join(adaptive_tid, NULL);
  }
  executor_shutdown();
  shutdown_memory_manager();
  exit(status);
}

static void interrupt(int sig){
  static int called = 0;
  if (called == 0) {
    called = 1;
    fprintf(stderr,"%c[2K\r", 27);
    fprintf(stderr,"Shutting down server...\n");
    global_exit(0);
  } else if (called == 1) {
    called = 2;
    fprintf(stderr,"%c[2K\r", 27);
    fprintf(stderr,"Waiting for worker threads to complete.\nInterrupt again to force quit (not recommended)\n");
  } else {
    fprintf(stderr,"%c[2K\r", 27);
    exit(1);
  }
}

#define DOC_BUFFER_LEN 160

static char doc[DOC_BUFFER_LEN];
static char args_doc[] = "PORT";

static struct argp_option options[] = {
  {"adaptive",  'a', 0, 0, "Run in adaptive mode. Connecting clients must also be run in adaptive mode" },
  {"directory", 'd', "DIR", 0, "Serve images from DIR, defaults to imgs/" },
  {"verbose",   'v', 0, 0, "Produce verbose output" },
  {"memdebug",  'm', 0, 0, "Print memory debugging messages if compiled with MEM_DEBUG" },
  { 0 }
};

struct arguments {
  int port;             /* arg1 */
  int adaptive, verbose, debug;   /* '-a', '-v', '-m' */
  char *img_dir;        /* directory arg to --directory */
};

static error_t parse_opt (int key, char *arg, struct argp_state *state) {
  /* Get the input argument from argp_parse, which we
    know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key){
  case 'a':
    arguments->adaptive = 1;
    break;
  case 'd':
    arguments->img_dir = arg;
    break;
  case 'v':
    arguments->verbose = 1;
    break;
  case 'm':
    arguments->debug = 1;
    break;

  case ARGP_KEY_ARG:
    if (state->arg_num == 0) {
      errno = 0;
      arguments->port = (int)strtol(arg,NULL,0);
      if (errno == ERANGE) { /* Not a number? or number too big? */
        argp_usage(state);
      }
    } else {
      argp_usage(state);
    }
    break;

  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char **argv) {
  program_name = basename(argv[0]);
  snprintf(doc,DOC_BUFFER_LEN,"%s -- a simple or adaptive image server for COMP-535\vIf PORT is omitted, a random free port will be used.",program_name);
  struct arguments arguments;
  
  /* Default values. */
  arguments.port = 0;
  arguments.img_dir = NULL;
  arguments.adaptive = 0;
  arguments.verbose = 0;
  arguments.debug = 0;
  
  argp_parse (&argp, argc, argv, 0, 0, &arguments);
  
  if (!arguments.img_dir)
    image_dir = "imgs/";
  else
    image_dir = arguments.img_dir;
  
  verbose_f = arguments.verbose;
  adaptive_f = arguments.adaptive;
  
  sockfd = -1;
  if (adaptive_f) {
    adaptivefd = -1;
    atid_v = 0;
  }
  signal(SIGINT, interrupt);
  start_memory_manager(arguments.debug);
  executor_init();
  
  int cfd, cid=1;
  socklen_t clilen;
  struct sockaddr_storage cli_addr;
  char s[INET6_ADDRSTRLEN];
  
  sockfd = create_and_bind_sock(arguments.port);
  if (sockfd == -1) {
    fprintf(stderr, "%s: failed to bind\n", program_name);
    global_exit(1);
  }
  if (listen(sockfd, 10) != 0) {
    perror("listen");
    close(sockfd);
    global_exit(1);
  }
  
  fprintf(stderr, "Listening on port %d.\n", get_port_num(sockfd));
  if (adaptive_f) {
    initialize_adaptive();
    
    adaptivefd = create_and_bind_sock(0);
    if (adaptivefd == -1) {
      fprintf(stderr, "%s: failed to bind adaptive\n", program_name);
      global_exit(1);
    }
    if (listen(adaptivefd, 10) != 0) {
      perror("listen");
      global_exit(1);
    }
    adaptiveport = get_port_num(adaptivefd);
    fprintf(stderr, "Scheduler on port %d.\n", adaptiveport);
    /* Launch adaptive thread */
    if ((errno = pthread_create(&adaptive_tid, NULL, adaptive_scheduler, NULL)) != 0) {
      perror("pthread_create");
      global_exit(1);
    }
    atid_v = 1;
  }
  
  clilen = sizeof(cli_addr);
  for (;;) {
    if ((cfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) < 0) {
      perror("accept");
      close(sockfd);
      global_exit(1);
    }
    inet_ntop(cli_addr.ss_family, get_in_addr((struct sockaddr *)&cli_addr),
      s, sizeof s);
    if (executor_execute(cfd,cid++,s) == -1) {
      char *reason;
      if (errno == EBUSY) {
        reason = "Worker limit reached";
      } else {
        reason = "Server is shutting down";
      }
      if (verbose_f)
        fprintf(stderr, "Client %s connection dropped: %s.\n", s, reason);
      else
        fprintf(stderr, "[%s] Reject\n", s);
      close(cfd);
    } else {
      if (!verbose_f)
        fprintf(stderr, "[%s] Accept\n", s);
    }
  }
  
  global_exit(0);
}