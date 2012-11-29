#ifndef MEMORY_H
#define MEMORY_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define ALLOC_N(type,n) (type*)emalloc(sizeof(type)*(n))
#define ALLOC(type) (type*)emalloc(sizeof(type))

void *emalloc(size_t size);
void efree(void *ptr);
void *ecalloc(size_t nmemb, size_t size);
void *erealloc(void *ptr, size_t size);
char *estrdup(const char *s);
char *estrndup(const char *s, unsigned int length);

#endif
