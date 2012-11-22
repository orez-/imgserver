#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ALLOC_N(type,n) (type*)emalloc(sizeof(type)*(n))
#define ALLOC(type) (type*)emalloc(sizeof(type))

#define MEM_BLOCK_END_MAGIC 0x2A8FCC84L

typedef struct _mem_header {
  struct _mem_header *pNext;
  struct _mem_header *pLast;
  unsigned int size;
} mem_header;

typedef union _align_test {
	void *ptr;
	double dbl;
	long lng;
} align_test;

#if (defined (__GNUC__) && __GNUC__ >= 2)
#define PLATFORM_ALIGNMENT (__alignof__ (align_test))
#else
#define PLATFORM_ALIGNMENT (sizeof(align_test))
#endif

#define MEM_HEADER_PADDING (((PLATFORM_ALIGNMENT-sizeof(mem_header))%PLATFORM_ALIGNMENT+PLATFORM_ALIGNMENT)%PLATFORM_ALIGNMENT)

void *emalloc(size_t size);
void efree(void *ptr);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *s);
char *estrndup(const char *s, unsigned int length);

void start_memory_manager(int _verbose);
void shutdown_memory_manager();

void _mem_block_check(mem_header *p);

#endif
