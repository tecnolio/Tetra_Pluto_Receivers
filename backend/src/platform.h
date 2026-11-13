#ifndef PT_PLATFORM_H
#define PT_PLATFORM_H

#ifdef __arm__

typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned short uint16_t;
typedef signed short int16_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
typedef unsigned int size_t;
typedef int ssize_t;
typedef long ptrdiff_t;

typedef struct FILE FILE;

/* FILE / stdio */
extern FILE *fopen(const char*, const char*);
extern int fclose(FILE*);
extern size_t fread(void*, size_t, size_t, FILE*);
extern size_t fwrite(const void*, size_t, size_t, FILE*);
extern int fflush(FILE*);
extern int fseek(FILE*, long, int);
extern long ftell(FILE*);
extern int fprintf(FILE*, const char*, ...);
extern int printf(const char*, ...);
extern int snprintf(char*, size_t, const char*, ...);
extern int fscanf(FILE*, const char*, ...);
extern int sscanf(const char*, const char*, ...);
extern char *fgets(char*, int, FILE*);

/* Memoria */
extern void *malloc(size_t);
extern void free(void*);
extern void *memcpy(void*, const void*, size_t);
extern void *memmove(void*, const void*, size_t);
extern void *memset(void*, int, size_t);

/* Strings */
extern size_t strlen(const char*);
extern int strcmp(const char*, const char*);
extern int strncmp(const char*, const char*, size_t);
extern char *strstr(const char*, const char*);

/* Conversiones */
extern int atoi(const char*);
extern double atof(const char*);
extern unsigned long strtoul(const char*, char**, int);

/* Ficheros */
extern int rename(const char*, const char*);
extern int remove(const char*);

/* Sistema */
extern int usleep(unsigned int);
extern int getpid(void);
extern void _exit(int);

/* Matemáticas */
extern float sinf(float);
extern float cosf(float);
extern float atan2f(float, float);
extern float sqrtf(float);
extern float fabsf(float);

extern double sin(double);
extern double cos(double);
extern double pow(double, double);
extern double floor(double);

#define NULL ((void*)0)

#else

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#endif

#endif
