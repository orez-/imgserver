#ifndef CLIENT_H
#define CLIENT_H

#define HAS_GNUREADLINE

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>      /* va_arg, va_start() */
#include <string.h>
#include <ctype.h>       /* isdigit() */
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <signal.h>
#include <argp.h>
#ifdef HAS_GNUREADLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif
#include "memory.h"

#define BUFFER_SIZE 1024
#define LINE_SIZE 256
#define MAX_FILE_BUFFER 1048576
#define DELIM ":"

typedef struct _readbuffer {
  size_t used;
  char data[BUFFER_SIZE];
} readbuffer;

#endif
