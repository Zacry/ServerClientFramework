#include "util.h"
#include "trace.h"
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/*utility function to display the function backtrace*/
void UtilPrintBackTrace(int taskIndex)
{
    void* array[BACK_TRACE_SIZE];
    int size;
    char** str;
    int i;
    char strTotal[BACK_TRACE_STR_LENGTH];
    char *pStrTotal = strTotal;
    char *headline = "backtrace for current step:\n\n";

    size = backtrace(array, BACK_TRACE_SIZE);
    str = backtrace_symbols(array, size);

    sprintf(pStrTotal, headline);
    pStrTotal += strlen(headline);

    for(i=0; i<size; i++)
    {
        sprintf(pStrTotal, "%2d. %s\n", i+1, str[i]);
        pStrTotal = strTotal + strlen(strTotal);
    }

    TRACE(TRACE_DEBUG, taskIndex, strTotal);
}


bitmap* bitmapCreat(int size)
{
    bitmap* pBm;
    char* pCh;
        
    if(size < 0 || size > BITMAP_MAX_SIZE)
    {
        pBm =  NULL;
    }
    else
    {
        pBm = (bitmap*)malloc(sizeof(bitmap));
        pBm->size = size;
        if(size % BITMAP_SIZE_PER_CHAR == 0)
        {
            pBm->bitmapStr = (char*)malloc(size/BITMAP_SIZE_PER_CHAR);
        }
        else
        {
            pBm->bitmapStr = (char*)malloc(size/BITMAP_SIZE_PER_CHAR + 1);
        }
        pCh = (char*)malloc(1);
    }
    
    return pBm;
}

bitmap_ret bitmapAllocate(bitmap* pBm, int* pIndex)
{
    bitmap_ret retVal = BITMAP_FAILURE;
    int index;
    char val = 0x1;
    
    if(pBm != NULL && pIndex != NULL)
    {
        for(index=0; index<pBm->size; index++)
        {
            if(index % BITMAP_SIZE_PER_CHAR == 0)
            {
                val = 0x1;
            }
            
            if((pBm->bitmapStr[index/BITMAP_SIZE_PER_CHAR] & val) == 0)
            {
                retVal = BITMAP_SUCCESS;
                break;
            }

            val <<= 1;
        }

        if(index < pBm->size)
        {
            pBm->bitmapStr[index/BITMAP_SIZE_PER_CHAR] |= 0x1 << (index%BITMAP_SIZE_PER_CHAR);
            *pIndex = index;
        }
    }

    

    return retVal;
}


bitmap_ret bitmapDeallocate(bitmap* pBm, int index)
{
    bitmap_ret retVal = BITMAP_FAILURE;

    if(pBm != NULL && ((pBm->bitmapStr[index/BITMAP_SIZE_PER_CHAR] & (0x1 << (index%BITMAP_SIZE_PER_CHAR))) != 0))
    {
        pBm->bitmapStr[index/BITMAP_SIZE_PER_CHAR] &= ~(0x1 << (index%BITMAP_SIZE_PER_CHAR));
        retVal = BITMAP_SUCCESS;
    }
    
    return retVal;    
}

