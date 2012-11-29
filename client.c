#include "client.h"

/**
 Globals
*/
const char *program_name;
static int sockfd;
static int adaptivefd;
static int verbose_f;
static int adaptive_f;
static int batch_f;
static readbuffer *buffer;

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

int connect_to_host(char *host, int port, char* buffer, int buflen) {
  int portlen, r, newfd;
  struct addrinfo hints, *servinfo, *p;
  char *portstr;
  
  memset(&hints, 0, sizeof hints); // make sure the struct is empty
  hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  
  portlen = snprintf(NULL, 0, "%d", port);
  portstr = (char *)emalloc(portlen+1);
  snprintf(portstr, portlen+1, "%d", port);
  if ((r = getaddrinfo(host, portstr, &hints, &servinfo)) != 0){
    fprintf(stderr,"getaddrinfo: %s\n", gai_strerror(r));
  }
  efree(portstr);
  
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((newfd = socket(p->ai_family, p->ai_socktype,
          p->ai_protocol)) == -1) {
      perror("socket");
      continue;
    }

    if (connect(newfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(newfd);
      // perror("connect");
      continue;
    }

    break;
  }
  if (p == NULL)
    newfd = -1;
  else if (buffer != NULL)
    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), buffer, buflen);
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

static int add_newline(char *buffer, int maxlen) {
  int l;
  l = strlen(buffer);
  if (l > maxlen - 2) {
    return 1; /* No space for newline */
  }
  buffer[l] = '\n';
  buffer[l+1] = '\0';
  return 0;
}

static void remove_newline(char *string) {
  int l = strlen(string);
  if (l == 0) return;
  if (string[l-1] == '\n') string[l-1] = '\0';
}

static int is_number(char *string) {
  if (strlen(string) == 0) return 0;
  char *p = string;
  int i, len = strlen(string);
  for (i = 0; i < len; ++i) {
    if (!isdigit(*p)) return 0;
    ++p;
  }
  return 1;
}

/**
  Main
*/
static void global_exit(int status) {
  if (sockfd != -1)
    close(sockfd);
  if (adaptive_f && adaptivefd != -1)
    close(adaptivefd);
  exit(status);
}

static void interrupt(int sig){
  static int called = 0;
  if (!called) {
    called = 1;
    fprintf(stderr,"%c[2D", 27);
		fprintf(stderr,"%c[0Kinterrupt\n", 27);
    global_exit(0);
  }
}

static char *recvline() {
  char *line, *start, *end;
  size_t r;
  size_t remain = BUFFER_SIZE - buffer->used;
  if (buffer->used > 0) {
    start = buffer->data;
    end = (char *)memchr((void *)start, '\n', buffer->used);
    if (end) { /* Found a newline */
      *end = '\0'; /* Replace newline with null to return */
      r = end - start;
      line = estrndup(start, r);
      buffer->used -= r + 1;
      start = end + 1;
      memmove(buffer->data, start, buffer->used);
      trim_in_place(line);
      return line;
    } else if (remain == 0) {
      fprintf(stderr, "Fatal: Line exceeds buffer length\n");
      global_exit(0);
    }
  }
  if (remain > 0) {
    r = recv(sockfd, (buffer->data+buffer->used), remain, 0);
    if (r == -1) {
      perror("recv");
      global_exit(1);
    } else if (r == 0) {
      fprintf(stderr,"Server dropped connection.\n");
      global_exit(1);
    }
    buffer->used += r;
    return recvline();
  }
}

#ifdef HAS_GNUREADLINE
char *snreadline(char *buffer, size_t length, char *prompt) {
  char *line;
  if (batch_f) {
    line = fgets(buffer, length, stdin);
  } else {
    line = readline(prompt);
  }
  if (line == NULL)
    return line;
  trim_in_place(line);
  if (batch_f) return line;
  size_t linelen = strlen(line);
  if (linelen > 0)
    add_history(line);
  size_t copy = linelen > length-1 ? length-1 : linelen;
  memcpy(buffer, line, copy);
  buffer[copy] = '\0';
  free(line);
  return buffer;
}
#endif

#define DOC_BUFFER_LEN 160

static char doc[DOC_BUFFER_LEN];
static char args_doc[] = "HOST PORT";

static struct argp_option options[] = {
  {"adaptive",  'a', 0, 0, "Run in adaptive mode. Server must also be run in adaptive mode" },
  {"verbose",   'v', 0, 0, "Produce verbose output" },
  {"silent",    's', 0, 0, "Produce no output" },
  {"batch",     'b', 0, 0, "Runs in batch mode, implies silent" },
  {"file",      'f', "FILE", 0, "Reads commands from FILE. Implies batch. Fails silently on bad argument." },
  {"nooutput",  'n', 0, 0, "Prevents writing to the filesystem" },
  { 0 }
};

struct arguments {
  int port;     /* arg1 */
  int adaptive, verbose, silent, batch, devnull;   /* '-a', '-v', '-m' */
  char *host, *infile;   /* arg2 */
};

static error_t parse_opt (int key, char *arg, struct argp_state *state) {
  /* Get the input argument from argp_parse, which we
    know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;

  switch (key){
  case 'a':
    arguments->adaptive = 1;
    break;
  case 'v':
    arguments->verbose = 1;
    break;
  case 's':
    arguments->silent = 1;
    break;
  case 'b':
    arguments->batch = 1;
    arguments->silent = 1;
    break;
  case 'f':
    arguments->infile = arg;
    arguments->batch = 1;
    arguments->silent = 1;
    break;
  case 'n':
    arguments->devnull = 1;
    break;

  case ARGP_KEY_ARG:
    if (state->arg_num == 0) {
      arguments->host = arg;
    } else if (state->arg_num == 1) {
      errno = 0;
      arguments->port = (int)strtol(arg,NULL,0);
      if (errno == ERANGE) { /* Not a number? or number too big? */
        argp_usage(state);
      }
    } else {
      argp_usage(state);
    }
    break;
    
  case ARGP_KEY_END:
    if (state->arg_num < 2)
      /* Not enough arguments */
      argp_usage(state);
    break;

  default:
    return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char **argv) {
  program_name = basename(argv[0]);
  snprintf(doc,DOC_BUFFER_LEN,"%s -- a simple client for COMP-535",program_name);
  struct arguments arguments;
  
  /* Default values. */
  arguments.port = -1;
  arguments.host = NULL;
  arguments.adaptive = 0;
  arguments.verbose = 0;
  arguments.silent = 0;
  arguments.batch = 0;
  arguments.devnull = 0;
  arguments.infile = NULL;
  
  argp_parse (&argp, argc, argv, 0, 0, &arguments);
  
  verbose_f = 0;
  if (arguments.verbose)
    verbose_f = 1;
  if (arguments.silent) {
    freopen("/dev/null", "w", stderr);
    freopen("/dev/null", "w", stdout);
  }
  batch_f = arguments.batch;
  adaptive_f = arguments.adaptive;
  if (arguments.infile) {
    if(freopen(arguments.infile, "r", stdin) == NULL){
      exit(1);
    }
  }
  
  sockfd = -1;
  if (adaptive_f)
    adaptivefd = -1;
  signal(SIGINT, interrupt);
  
  char foldertmp[] = "clientimgXXXXXX";
  char s[INET6_ADDRSTRLEN], *t, *line, linebuf[LINE_SIZE];
  char *filename, *filebuf;
  int cid, l; /* Return values, temp values */
  FILE *file;
  size_t filesize, read, remain, total, chunk;
  
  sockfd = connect_to_host(arguments.host, arguments.port, s, sizeof s);
  if (sockfd == -1) {
    fprintf(stderr, "failed to connect to server.\n");
    global_exit(1);
  }
  fprintf(stdout, "Connected to %s\n", s);
  
  buffer = ALLOC(readbuffer);
  buffer->used = 0;
  
  // HELLO YES THIS IS SERVER
  line = recvline();
  t = strtok(line,DELIM);
  if (strcmp(t, "HELLO") != 0){
    fprintf(stderr,"Unexpected message from server: %s\nExiting.\n", line);
    global_exit(0);
  }
  t = strtok(NULL,DELIM);
  cid = (int)strtol(t,NULL,0);
  if (errno == ERANGE) { /* Not a number? or number too big? */
    fprintf(stderr, "Invalid client ID from server. Exiting");
    global_exit(1);
  }
  fprintf(stdout, "Got client ID: %d\n", cid);
  if (adaptive_f) {
    if ((t = strtok(NULL,DELIM)) == NULL){
      fprintf(stderr, "Server is not in adaptive mode. Exiting\n");
      global_exit(1);
    }
    errno = 0;
    l = (int)strtol(t,NULL,0);
    if (errno == ERANGE) { /* Not a number? or number too big? */
      fprintf(stderr, "Invalid port number from server. Exiting");
      global_exit(1);
    }
    adaptivefd = connect_to_host(arguments.host, l, NULL, 0);
    if (adaptivefd == -1) {
      fprintf(stderr, "failed to connect to adaptive server.\n");
      global_exit(2);
    }
    snprintf(linebuf, LINE_SIZE, "%d\n", cid);
    if (send(adaptivefd, linebuf, strlen(linebuf), 0) == -1){
      perror("send");
      global_exit(1);
    }
  }
  efree(line);
  if (!arguments.devnull){
    mkdtemp(foldertmp);
    fprintf(stdout, "Storing downloaded images in directory %s.\n", foldertmp);
  }
  
  for (;;) {
#ifdef HAS_GNUREADLINE
    if(snreadline(linebuf, LINE_SIZE, "GET> ") == NULL){
#else
    if (!batch_f)
      fprintf(stderr, "GET> ");
    if(fgets(linebuf, LINE_SIZE, stdin) == NULL){
#endif
      printf("exit\n");
      global_exit(0);
    }
#ifndef HAS_GNUREADLINE
    trim_in_place(linebuf);
#endif
    if (strlen(linebuf) == 0) continue;
    /* Hacky hack to transmit panning speed, any 2 or less digit
       number is considered a pan speed */
    if (adaptive_f && strlen(linebuf) < 3 && is_number(linebuf)) {
      add_newline(linebuf, LINE_SIZE);
      if (send(adaptivefd, linebuf, strlen(linebuf), 0) == -1){
        perror("send");
        global_exit(1);
      }
      continue;
    }
    if (add_newline(linebuf, LINE_SIZE)) {
      fprintf(stderr, "Command too long.\n");
      continue;
    }
    if (send(sockfd, linebuf, strlen(linebuf), 0) == -1){
      perror("send");
      global_exit(1);
    }
    remove_newline(linebuf);
    line = recvline();
    t = strtok(line,DELIM);
    if (strcmp(t, "ERROR") == 0){
      t = strtok(NULL,DELIM);
      fprintf(stderr, "Server> Error: %s\n", t);
    } else if (strcmp(t, "FILE") == 0) {
      t = strtok(NULL,DELIM);
      errno = 0;
      size_t filesize = (size_t)strtol(t,NULL,0);
      if (errno == ERANGE) {
        fprintf(stderr,"Fatal Error: Could not parse file size.\n", t);
        global_exit(0);
      }
      if (!arguments.devnull) {
        l = snprintf(NULL, 0, "%s/%s", foldertmp, linebuf);
        filename = (char *)emalloc(l+1);
        snprintf(filename, l+1, "%s/%s", foldertmp, linebuf);
      } else {
        filename = (char *)emalloc(10);
        snprintf(filename, 10, "/dev/null");
      }
      file = fopen(filename,"wb");
      efree(filename);
      chunk = (filesize > MAX_FILE_BUFFER) ? MAX_FILE_BUFFER : filesize;
      filebuf = (char *)emalloc(chunk);
      remain = filesize;
      total = 0;
      if (buffer->used > 0){
        fwrite(buffer->data, 1, buffer->used, file);
        total += buffer->used;
        remain -= buffer->used;
        buffer->used = 0;
      }
      fprintf(stderr,"'%s' [%ld/%ld] (%ld%%)", linebuf, (long)total, (long)filesize, (long)(100*total)/filesize);
      while (remain > 0) {
        read = recv(sockfd, filebuf, chunk, 0);
        if (read == -1) {
          perror("recv");
          global_exit(3);
        } else if (read == 0) {
          fprintf(stderr,"Server dropped connection.\n");
          global_exit(4);
        }
        total += read;
        remain -= read;
        fprintf(stderr,"%c[2K\r", 27);
        fprintf(stderr,"'%s' [%ld/%ld] (%ld%%)", linebuf, (long)total, (long)filesize, (long)(100*total)/filesize);
        fwrite(filebuf, 1, read, file);
      }
      fprintf(stderr,"%c[2K\r", 27);
      printf("'%s' saved. [%ld/%ld]\n", linebuf, (long)total, (long)filesize);
      fclose(file);
      efree(filebuf);
    } else {
      verbose("Ignoring unexpected message from server: %s\n", t);
    }
    efree(line);
  }
}
