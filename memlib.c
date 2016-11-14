#include "memlib.h"

#define MEMORY_SIZE 1024*1024*8
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

static char *memory_space = NULL;	//total memory
void *memory_base = NULL;

//for memory allocation within a thread
typedef struct thread_block_meta
{
	size_t size;		// shows space can be allocated. exclude the size of meta block
	int isFree;			// 1 is for free; 0 for not free
	//TODO: Need to add more members?
}tb_meta;
#define THREAD_META_SIZE sizeof(struct thread_block_meta)

/*Page table part*/
typedef struct pt_node
{
	int owner_id;		//thread id
	int page_num;			//page_number
	struct pt_node *next;		//indicate the next page
	void *map_to_addr;  //the physical memory this page mapped to
	void *current_addr;	//the physical memory this page currently swapped to
	int padding;		//only for 8 bytes alignment in x86, useless
}pt_node;

static pt_node page_table[2048];

//initialize page table
void init_page_table()
{
	for (int i = 0 ; i < 2048 ; i++)
	{
		page_table[i].owner_id = -1;	//unused node has id -1
		page_table[i].page_num= -1;
		page_table[i].next = NULL;
		page_table[i].map_to_addr = NULL;
		page_table[i].current_addr = NULL;
	}
}

/*Instrument part*/
//TODO: Finish this function
void defragmentation()
{

}

//////////////////////////////////////////////////////////////
/* Description to swap:
 * First check if the swap_to memory is used by other threads
 * Then swap that page to a free place and mprotect that page
 * In this way, we must guarantee that there is a free page to
 * do the swap operation
 * If queue_size() > 0 we can swap within memory; else swap to
 * file -- PHASE C
 * last, unprotect the swap_from page and swap it to the target
 * */
//////////////////////////////////////////////////////////////

//TODO: Finish this function
void swap(void *swap_from, void *swap_to, int page_index)
{
	//First check if the swap_to memory is used by other threads
	if (swap_from == swap_to)
		return;
	
	int i = 0;
	while (i < 2048 && page_table[i].current_addr != swap_to)
		i++;
	//in this case, the target memory is not in use
	if (i == 2048)
	{
		memcpy(swap_to, swap_from, PAGE_SIZE);
		page_table[page_index].current_addr = swap_to;
		mprotect(swap_to, PAGE_SIZE, PROT_READ | PROT_WRITE);

		//swap_to = memory_base + page_number * PAGE_SIZE;
		//page_number = (swap_to - memory_base) / PAGE_SIZE;
		enqueue(page_table[page_index].page_num);
		page_table[page_index].page_num = ((char *)swap_to - (char *)memory_base) / PAGE_SIZE;

		//remove the page from the free page queue...dirty part :(
		//in this case, the free page queue must not be empty, right?
		while (page_table[page_index].page_num != peek())
			enqueue(dequeue());
		
		int rcv = dequeue();
		//DEBUG
		if (rcv != page_table[page_index].page_num)
			printf("Error in swap!\n");
	}
	//in this case, the target memory is used by other thread
	else
	{
		mprotect(swap_to, PAGE_SIZE, PROT_READ | PROT_WRITE);
		mprotect(swap_from, PAGE_SIZE, PROT_READ | PROT_WRITE);
		//PHASE B; Need to modify in phase C
		int page_number;
		if (queue_size > 0)
		{
			//swap the swap_to page to the new free page
			page_number = dequeue();
			void *target = (void *)((char *) memory_base + page_number * PAGE_SIZE);
			memcpy(target, page_table[i].current_addr, PAGE_SIZE);
			page_table[i].current_addr = target;
			mprotect(page_table[i].current_addr, PAGE_SIZE, PROT_NONE);

			//move the swap_from page to the target place
			memcpy(swap_to, swap_from, PAGE_SIZE);
			page_table[page_index].current_addr = swap_to;

			//modify the page_number part in page table; enqueue the source page 
			enqueue(page_table[page_index].page_num);
			page_table[page_index].page_num = page_table[i].page_num;
			page_table[i].page_num = page_number;
		}
		else
			//PHASE C: swap to file
			;
	}

}

///////////////////////////////////////////////////////////////
/* Solution: Once find_free_space failed, call request space to 
 * allocate more page to this block. We need to modify the page
 * table and block_meta of this thread_id.
 * */
///////////////////////////////////////////////////////////////

/* Description: once failed to find a free space in mallocated
 * memory, we call this function to request more pages. This 
 * function only modify the page table and the free memory queue
 * We need to process the swap issues in other function
 *
 * @return: 1 on success; 0 on failed
 * */ 
int request_space(size_t size, block_meta *block)
{
	//calculate how many pages we need to allocate
	size_t num_of_pages = (size + THREAD_META_SIZE - 1) / PAGE_SIZE + 1;
	if (queue_size() < num_of_pages)
		return 0;

	//go to the tail of the link
	pt_node *node = &page_table[block->page_table_index];
	while (node->next != NULL)
		node = node->next;

	for (size_t j = 0 ; j < num_of_pages ; j++)
	{
		int page_number = dequeue();
		//insert page table
		int i = 0;
		while (i < 2048 && page_table[i].owner_id != -1)
			i++;
		if (i == 2048)
			return 0;
		page_table[i].owner_id = current_thread_id;
		page_table[i].page_num = page_number;
		page_table[i].map_to_addr = (void *)((char*)(block + 1) + block->size);	
		page_table[i].current_addr = (void *)((char*)memory_base + page_number * PAGE_SIZE);
		page_table[i].next = NULL;

		node->next = &page_table[i];
		node = node->next;

		block->size += PAGE_SIZE;
	}
	return 1;
}

/*@return: the thread block meta if success; NULL if no space to allocate
 *@comment: may result in waste of memory if we request more memory at first
 *time
 * */
//TODO: check correctness
void *find_free_space(size_t size, block_meta *block)
{
	tb_meta *t_block = (tb_meta *)(block + 1);
	int mark = 0;
	size_t size_counter = 0;
	while (size_counter < block->size)
	{
		if (t_block->isFree == 1 && t_block->size >= size)
		{
			if (t_block->size < size + THREAD_META_SIZE + 1)
			{	
				//return the whole space, because the remaining space is not enough for a meta block
				t_block->isFree = 0;
			}	
			else
			{
				//write the block meta information for the remaining memory space 
				tb_meta *temp = t_block;
				temp = (tb_meta *)((char *)(temp + 1) + size);
				temp->isFree = 1;
				temp->size = t_block->size - size - THREAD_META_SIZE;

				/*We can add defragmentation operation function here later*/

				t_block->isFree = 0;
				t_block->size = size;
			}
			mark = 1;
			break;
		}

		//increment counter and move the t_block to the next
		size_counter += (THREAD_META_SIZE + t_block->size);
		t_block = (tb_meta*)((char*)(block + 1) + size_counter);		
	}
	/* Phase A part
	if (mark == 1)
		return t_block;
	else
		return NULL;
		*/

	//Phase B
	if (mark == 1)
		return t_block;
	/* If we need to request new pages, we assume always start to allocate 
	 * from the new page, even though there are some remaining spaces at the
	 * end of the first page
	 * */
	else if (request_space(size, block) == 1)
	{	
		//calculate how many pages we allocated
		size_t num_of_pages = (size + THREAD_META_SIZE - 1) / PAGE_SIZE + 1;

		t_block->isFree = 0;
		t_block->size = size;

		tb_meta *temp = (tb_meta *)((char *)(t_block + 1) + size);
		temp->isFree = 1;
		temp->size = num_of_pages * PAGE_SIZE - size - 2 * THREAD_META_SIZE;
		return t_block;
	}
	else
		return NULL;	//no more space, request space failed

}

/*Instrument part end*/

void *malloc_lib(size_t size)
{
	block_meta *block;
	tb_meta *t_block;

	if (queue_size() > 0)
	{
		int page_number = dequeue();
		block = (block_meta *)((char*)memory_base + page_number * PAGE_SIZE);
		block->size = PAGE_SIZE - META_SIZE;
		block->owner_id = current_thread_id;

		//insert page table
		int i = 0;
		while (i < 2048 && page_table[i].owner_id != -1)
			i++;
		//TODO: Can this case happens??
		if (i == 2048)
			return NULL;
		block->page_table_index = i;
		page_table[i].owner_id = current_thread_id;
		page_table[i].page_num= page_number;
		page_table[i].map_to_addr = block;
		page_table[i].current_addr = block;

		//initialize the thread block
		t_block= (tb_meta*)(block + 1);
		t_block->size = PAGE_SIZE - META_SIZE - THREAD_META_SIZE;
		t_block->isFree = 1;
	}
	else
		return NULL;
	return block;
}

void *malloc_thread(size_t size)
{
	//find the page of current thread
	block_meta *block;
	int i;
	for (i = 0 ; i < 2048 ; i++)
	{
		if (page_table[i].owner_id == current_thread_id)
		{
			//Phase A; later we need to consider about the swap in Phase B here
			block = page_table[i].map_to_addr;
			break;
		}
	}

	//check the allocated_size and find the free place to allocate memory
	tb_meta *t_block = find_free_space(size, block);
	return t_block;
}
 
void *myallocate(size_t size, char FILE[], int LINE, int type)
{
	if (size <= 0 || size >= 4096)
	{
		return NULL;
	}

	//first call, initialize queue and page table
	if (memory_base == NULL)
	{
		init_queue();
		init_page_table();
		memory_space = (char*) memalign(PAGE_SIZE , MEMORY_SIZE * sizeof(char));
		memory_base = (void*) memory_space;
	}

    //choose which function to deal with the request
	if (type == THREADREQ)
	{
		tb_meta *t_block = (tb_meta*)malloc_thread(size);
		if (t_block == NULL)
			return NULL;
		else
			return t_block + 1;
	}
	else if (type == LIBRARYREQ)                                                                                                             	 {		
		block_meta *block = (block_meta *)malloc_lib(size);
		if (block == NULL)
			return NULL;
		else
			return block + 1;
	}
	else 
		printf("Wrong request type!\n");
	return NULL; 
}

void mydeallocate(void *ptr, char FILE[], int LINE, int type)
{
	if (!ptr)
		return;
	if (type == THREADREQ)
	{
		tb_meta *t_block = ((tb_meta *) ptr) - 1;
		t_block->isFree = 1;
	}
	else if (type == LIBRARYREQ)
	{
		block_meta *block = ((block_meta *) ptr) - 1;

		int i = block->page_table_index;
		pt_node *node = &page_table[i];
		//check the linked list to find all the pages under current thread
		//id and reset its page table content, enqueue the page number
		while (node != NULL)
		{
			//reset page table & add freed page to the queue
			node->owner_id = -1;
			node->map_to_addr = NULL;
			node->current_addr = NULL;
			enqueue(node->page_num);
			node->page_num = -1;
			if (node->next != NULL)
			{
				pt_node *temp = node;
				node = node->next;
				temp->next = NULL;
			}
			else
				node = node->next;
		}
	}
	else
		printf("Wrong request type!\n");
}

int main()
{
	int thread_id_1 = 1;
	int thread_id_2 = 2;
    int thread_id_3 = 3;

	current_thread_id = 1;
	void *thread_1 = myallocate(4000, NULL, 0, LIBRARYREQ);
	current_thread_id = 2;
	void *thread_2 = myallocate(4000, NULL, 0, LIBRARYREQ);

	printf("thread 1 address: %x\n", thread_1);
	printf("thread 2 address: %x\n", thread_2);

	current_thread_id = 1;
	void *t1_proc1 = myallocate(4056, NULL, 0, THREADREQ);
	current_thread_id = 1;
	mydeallocate(t1_proc1, NULL, 0, THREADREQ);
	void *t1_proc2 = myallocate(1, NULL, 0, THREADREQ);

	current_thread_id = 2;
	mydeallocate(thread_2, NULL, 0, LIBRARYREQ);
	current_thread_id = 3;
	void *thread_3 = myallocate(4000, NULL, 0, LIBRARYREQ);

	printf("thread 1 proc 1 address: %x\n", t1_proc1);
	printf("thread 1 proc 2 address: %x\n", t1_proc2);

	return 0;
}
