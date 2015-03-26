#ifndef _UTIL_H
#define _UTIL_H

#define BACK_TRACE_SIZE 10
#define BACK_TRACE_STR_LENGTH 1024

void UtilPrintBackTrace(int taskIndex);

typedef enum{
    BITMAP_SUCCESS = 0,
    BITMAP_FAILURE
}bitmap_ret;

#define BITMAP_MAX_SIZE 1000
#define BITMAP_SIZE_PER_CHAR 8

typedef struct{
    int size;
    char* bitmapStr;
}bitmap;

bitmap* bitmapCreat(int size);
bitmap_ret bitmapAllocate(bitmap* pBm, int* pIndex);
bitmap_ret bitmapDeallocate(bitmap* pBm, int index);

#endif
