#ifndef MEMLIB_H
#define MEMLIB_H
#include <stdlib.h>
#include <stdio.h>
#include "my_queue.h"
#include <unistd.h>

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
    //TODO: more members to be added?
	int owner_id;
	int page_num;
	int page_table_index;
}block_meta;
#define META_SIZE sizeof(struct block_meta)

void *myallocate(size_t size, char FILE[], int LINE, int type);

void mydeallocate(void *ptr, char FILE[], int LINE, int type);


#endif
