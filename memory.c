#include "memory.h"

mem_header *head;
int verbose;

#ifdef MEM_DEBUG
# define END_MAGIC_SIZE sizeof(long)
static long mem_block_end_magic = MEM_BLOCK_END_MAGIC;
#else
# define END_MAGIC_SIZE 0
#endif

#define REMOVE_POINTER_FROM_LIST(p)	\
  if (p==head) {                 \
    head = p->pNext;					   \
  } else {                       \
    p->pLast->pNext = p->pNext;  \
  }	\
  if (p->pNext) {                \
    p->pNext->pLast = p->pLast;	 \
  }

#define ADD_POINTER_TO_LIST(p)		\
  p->pNext = head;   \
  if (head) {        \
    head->pLast = p; \
  }                  \
  head = p;          \
	p->pLast = (mem_header *) NULL;

void *emalloc(size_t size) {
  unsigned int real_size = ((size+7) & ~0x7);
  mem_header *p = (mem_header *) malloc(sizeof(mem_header) + MEM_HEADER_PADDING + real_size + END_MAGIC_SIZE);
  if (!p){
    fprintf(stderr,"FATAL:  emalloc():  Unable to allocate %ld bytes\n", (long) size);
    exit(1);
  }
  ADD_POINTER_TO_LIST(p);
  p->size = size;
#ifdef MEM_DEBUG
  memcpy((((char *) p) + sizeof(mem_header) + MEM_HEADER_PADDING + size), &mem_block_end_magic, sizeof(long));
#endif
  return (void *)((char *)p + sizeof(mem_header) + MEM_HEADER_PADDING);
}
void efree(void *ptr) {
  mem_header *p = (mem_header *) ((char *)ptr - sizeof(mem_header) - MEM_HEADER_PADDING);
#ifdef MEM_DEBUG
  _mem_block_check(p);
#endif
  REMOVE_POINTER_FROM_LIST(p);
  free(p);
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
  mem_header *p;
  unsigned int real_size = ((size+7) & ~0x7);
  p = (mem_header *) ((char *)ptr-sizeof(mem_header)-MEM_HEADER_PADDING);
  REMOVE_POINTER_FROM_LIST(p);
  p = (mem_header *) realloc(p, sizeof(mem_header)+MEM_HEADER_PADDING+real_size+END_MAGIC_SIZE);
  if (!p) {
    fprintf(stderr,"FATAL:  erealloc():  Unable to allocate %ld bytes\n", (long) size);
    efree(ptr);
    exit(1);
  }
  ADD_POINTER_TO_LIST(p);
  p->size = size;
#ifdef MEM_DEBUG
  memcpy((((char *) p) + sizeof(mem_header) + MEM_HEADER_PADDING + size), &mem_block_end_magic, sizeof(long));
#endif
  return (void *)((char *)p+sizeof(mem_header)+MEM_HEADER_PADDING);
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

void start_memory_manager(int _verbose){
  head = NULL;
  verbose = _verbose;
}
void shutdown_memory_manager(){
  mem_header *p, *t;
#ifdef MEM_DEBUG
	int had_leaks = 0;
  unsigned int total_leak = 0;
  int total_leak_count = 0;
#endif
  p = head;
  t = head;
  while (t) {
#ifdef MEM_DEBUG
    had_leaks = 1;
    ++total_leak_count;
    total_leak += t->size;
    if (verbose) {
      // fprintf(stderr,"Freeing %p (%d bytes)\n", t, t->size);
      _mem_block_check(t);
    }
#endif
    p = t->pNext;
    REMOVE_POINTER_FROM_LIST(t);
    free(t);
    t = p;
  }
#ifdef MEM_DEBUG
	if (had_leaks && verbose)  {
    fprintf(stderr,"=== Total %d memory leaks detected (%u bytes total) ===\n", total_leak_count, total_leak);
  }
#endif
}
#ifdef MEM_DEBUG
void _mem_block_check(mem_header *p) {
  if (!verbose)
    return;
  long end_magic;
  memcpy(&end_magic, (((char *) p)+sizeof(mem_header)+MEM_HEADER_PADDING+p->size), sizeof(long));
  if (end_magic != MEM_BLOCK_END_MAGIC) {
    char *overflow_ptr, *magic_ptr=(char *) &mem_block_end_magic;
    int overflows=0;
    int i;

    overflow_ptr = (char *) &end_magic;

    for (i=0; i<sizeof(long); i++) {
      if (overflow_ptr[i]!=magic_ptr[i]) {
        overflows++;
      }
    }

    fprintf(stderr,"Block %p ", p);
    fprintf(stderr,"overflown (magic=0x%.8lX instead of 0x%.8lX)\n", end_magic, MEM_BLOCK_END_MAGIC);
    fprintf(stderr,"                ");
    if (overflows>=sizeof(long)) {
      fprintf(stderr,"At least %d bytes overflown\n", sizeof(long));
    } else {
      fprintf(stderr,"%d byte(s) overflown\n", overflows);
    }
  }
}
#endif