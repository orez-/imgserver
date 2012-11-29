#include "memory.h"

void *emalloc(size_t size) {
  void *p = malloc(size);
  if (!p) {
    fprintf(stderr,"FATAL:  emalloc():  Unable to allocate %ld bytes\n", (long) size);
    exit(1);
  }
  return p;
}
void efree(void *ptr) {
  free(ptr);
}
void *ecalloc(size_t nmemb, size_t size) {
  void *p;
  int final_size = size*nmemb;
  p = emalloc(final_size);
  memset(p, 0, final_size);
  return p;
}
void *erealloc(void *ptr, size_t size) {
  if (!ptr) {
    return emalloc(size);
  }
  void *p = realloc(ptr, size);
  if (!p) {
    fprintf(stderr,"FATAL:  erealloc():  Unable to allocate %ld bytes\n", (long) size);
    efree(ptr);
    exit(1);
  }
  return p;
}
char *estrdup(const char *s) {
  int length;
  char *p;
  
  length = strlen(s)+1;
  p = (char *) emalloc(length);
  memcpy(p, s, length);
  return p;
}
char *estrndup(const char *s, unsigned int length) {
  char *p;
  
  p = (char *) emalloc(length+1);
  memcpy(p,s,length);
  p[length] = 0;
  return p;
}
