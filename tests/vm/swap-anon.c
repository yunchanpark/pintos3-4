/* Checks if anonymous pages 
 * are swapped out and swapped in properly 
 * For this test, Pintos memory size is 10MB 
 * First, allocates big chunks of memory, 
 * does some write operations on each chunk, 
 * then check if the data is consistent
 * Lastly, frees the allocated memory. */

#include <string.h>
#include <stdint.h>
#include <syscall.h>
#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>


#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define ONE_MB (1 << 20) // 1MB
#define CHUNK_SIZE (20*ONE_MB) // 2^10 * 5
#define PAGE_COUNT (CHUNK_SIZE / PAGE_SIZE)

static char big_chunks[CHUNK_SIZE];

void
test_main (void) 
{
	size_t i;
    void* pa;
    char *mem;
    for (i = 0 ; i < PAGE_COUNT ; i++) {
        if(!(i % 512))
            msg ("write sparsely over page %zu", i);
        mem = (big_chunks+(i*PAGE_SIZE));
        *mem = (char)i;
    }

    msg("__debug : inininin\n");

    for (i = 0 ; i < PAGE_COUNT ; i++) {
        mem = (big_chunks+(i*PAGE_SIZE));
        printf("%c, %c",(char)i,*mem);
        if((char)i != *mem) {
            printf("\n\nFAIL here: %c, %c\n", (char)i, *mem);
            printf("%zu\n",i);
		    fail ("data is inconsistent");
        }
        if(!(i % 512))
            msg ("check consistency in page %zu", i);
    }
}

