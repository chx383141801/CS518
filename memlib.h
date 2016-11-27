#ifndef MEMLIB_H
#define MEMLIB_H
#include <stdlib.h>
#include <stdio.h>
#include "my_queue.h"
#include <unistd.h>
#include <sys/mman.h>
#include <string.h>
#include <malloc.h>
#include <signal.h>
<<<<<<< HEAD

=======
>>>>>>> 642957ffbe005d1fcdb3e0f9cb6a3ce7584ef284
#define THREADREQ  1 
#define LIBRARYREQ 2 

/*
 * memory structure
 * --------------------------------------
 *| meta | page_size - sizeof(META_SIZE) |
 * -------------------------------------- 
 */

//Thread scheduler **MUST** modify this value to tell memlib which is current running thread!
int current_thread_id;

/*store meta data of allocated memory (library call)*/
typedef struct block_meta
{
	size_t size;
	int owner_id;
	int reservation;		//8 bytes alignment for x86, or for future use
	int page_table_index;
}block_meta;
#define META_SIZE sizeof(struct block_meta)

void *myallocate(size_t size, char FILE[], int LINE, int type);

void mydeallocate(void *ptr, char FILE[], int LINE, int type);

void swap_in(void *target, int page_index);

int swap(void *swap_from, void *swap_to, int page_index);

int swap_out(void *source, int page_index);

#endif
