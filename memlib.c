#include "memlib.h"

#define MEMORY_SIZE		    1024*1024*8
#define PAGE_TABLE_SIZE	    2048*3
#define EXTERN_TABLE_SIZE   4096 
#define PAGE_SIZE sysconf(_SC_PAGE_SIZE)

static FILE *swap_fptr = NULL;
static char *memory_space = NULL;	//total memory
void *memory_base = NULL;

//for memory allocation within a thread
typedef struct thread_block_meta
{
	size_t size;		// shows space can be allocated. exclude the size of meta block
	int isFree;			// 1 is for free; 0 for not free
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
	int isFirst;		//mark if this is the first page of this thread
	int isExtern;		//if this page is on external disk
	int extern_index;	//only for external pages
}pt_node;

static pt_node page_table[PAGE_TABLE_SIZE];
static size_t pt_size = 0;		//the number of pages that already in use
static int extern_table[EXTERN_TABLE_SIZE];		//recording the usage status of external storage

//initialize page table
void init_page_table()
{
	for (int i = 0 ; i < PAGE_TABLE_SIZE ; i++)
	{
		page_table[i].owner_id = -1;	//unused node has id -1
		page_table[i].page_num= -1;
		page_table[i].next = NULL;
		page_table[i].map_to_addr = NULL;
		page_table[i].current_addr = NULL;
		page_table[i].isFirst = 0;		//0 means not the first
		page_table[i].isExtern = 0;	//0 means on physical memory (overlap with extern_index?? whatever...Memory is free!! LOL)
		page_table[i].extern_index = -1;//-1 means its an internal page
	}
}

void init_extern_table()
{
	for (int i = 0 ; i < EXTERN_TABLE_SIZE ; i++)
		extern_table[i] = 0;	//0 means this part is not in use
}

/*Instrument part*/
//Not a good implementation; only merge the memory after the given block
void thread_mem_merge(tb_meta* t_block) 
{
	while (((tb_meta*)((char*)(t_block + 1) + t_block->size))->isFree == 1)
		t_block->size += (((tb_meta*)((char*)(t_block + 1) + t_block->size))->size + THREAD_META_SIZE);
}

/* @Description: swap the page to disk
 * @return: 1 on success; 0 on failed
 * */
int swap_out(void *swap_from, int page_index)
{
	//we must unprotect the memory which we are doing operations to
	mprotect(swap_from, PAGE_SIZE, PROT_READ | PROT_WRITE);
	if (pt_size - (2048 - queue_size()) >= EXTERN_TABLE_SIZE)
		return 0;

	swap_fptr = fopen("temp.swap", "rb+");
	if (swap_fptr == NULL)
	{
		printf("Error in opeing swap file, error at swap_out\n");
		return 0;
	}
	int offset;
	for (offset = 0 ; offset < EXTERN_TABLE_SIZE ; offset++)
	{
		if (extern_table[offset] == 0)
			break;
	}
	page_table[page_index].extern_index = offset;
	page_table[page_index].isExtern = 1;

	extern_table[offset] = 1;
	
	enqueue(page_table[page_index].page_num);
	page_table[page_index].page_num = -1;
	
	fseek(swap_fptr, offset * PAGE_SIZE, SEEK_SET);
	fwrite(page_table[page_index].current_addr, sizeof(char), PAGE_SIZE, swap_fptr);

	page_table[page_index].current_addr = NULL;	

	fclose(swap_fptr);
	swap_fptr = NULL;
	
	return 1;
}

/* @Description: swap a page from disk to memory. We assume that the 
 *  target memory address is not in use. If it is in use, we must call
 *  swap_out to move the page in that memory address to another place.
 * */
void swap_in(void *target, int page_index)
{
	//We must unprotect the memory which we are doing operations to
	mprotect(target, PAGE_SIZE, PROT_READ | PROT_WRITE);
	swap_fptr = fopen("temp.swap", "rb+");
	if (swap_fptr == NULL)
	{
		printf("Error in opeing swap file, error at swap_in\n");
		return;
	}

	int offset = page_table[page_index].extern_index;
	fseek(swap_fptr, offset * PAGE_SIZE, SEEK_SET);
	fread(page_table[page_index].map_to_addr, sizeof(char), PAGE_SIZE, swap_fptr);

	page_table[page_index].current_addr = page_table[page_index].map_to_addr;
	page_table[page_index].isExtern = 0;
	page_table[page_index].extern_index = -1;
	page_table[page_index].page_num = ((char*)page_table[page_index].map_to_addr - (char*)memory_base) / PAGE_SIZE;

	//remove the page from the free page queue...dirty part again :( :(
	while (page_table[page_index].page_num != peek())
		enqueue(dequeue());

	int rcv = dequeue();
	//DEBUG
	if (rcv != page_table[page_index].page_num)
		printf("Error in swap_in!\n");
	
	extern_table[offset] = 0;

	fclose(swap_fptr);
	swap_fptr = NULL;
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

int swap(void *swap_from, void *swap_to, int page_index)
{
	//Unprotect the memory which we will do operations to
	mprotect(swap_to, PAGE_SIZE, PROT_READ | PROT_WRITE);
	mprotect(swap_from, PAGE_SIZE, PROT_READ | PROT_WRITE);

	//First check if the swap_to memory is used by other threads
	if (swap_from == swap_to)
		return 1;
	
	int i = 0;
	while (i < PAGE_TABLE_SIZE && page_table[i].current_addr != swap_to)
		i++;
	//in this case, the target memory is not in use
	if (i == PAGE_TABLE_SIZE)
	{
		memcpy(swap_to, swap_from, PAGE_SIZE);
		page_table[page_index].current_addr = swap_to;

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
		int page_number;
		if (queue_size > 0)
		{
			//swap the swap_to page to the new free page
			page_number = dequeue();
			void *target = (void *)((char *) memory_base + page_number * PAGE_SIZE);
			mprotect(target, PAGE_SIZE, PROT_READ | PROT_WRITE);

			memcpy(target, page_table[i].current_addr, PAGE_SIZE);
			page_table[i].current_addr = target;

			//protect the swapped out page
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
		{	
			//PHASE C: swap to file
			page_number = page_table[i].page_num;
			if (swap_out(page_table[i].current_addr, i) == 0)
				return 0;

			//move the swap_from page to the target place
			memcpy(swap_to, swap_from, PAGE_SIZE);
			page_table[page_index].current_addr = swap_to;

			//modify the page_number part in page table; enqueue the source page
			enqueue(page_table[page_index].page_num);
			page_table[page_index].page_num = page_number;

			//clear the queue number.....SHT!!!!WHY I USE QUEUE AT BEGINNING!!!!! T.T
			while (page_number != peek())
				enqueue(dequeue());
			dequeue();
		}
	}
	//set protection to the memory which the page swap from
	mprotect(swap_from, PAGE_SIZE, PROT_NONE);

	return 1;
}

///////////////////////////////////////////////////////////////
/* Solution: Once find_free_space failed, call request space to 
 * allocate more page to this block. We need to modify the page
 * table and block_meta of this thread_id.
 * */
///////////////////////////////////////////////////////////////

/*These two functions will be called by request_space*/
//TODO: check correctness
int request_internal(size_t size, block_meta *block)
{
	//calculate how many pages we need to allocate
	size_t num_of_pages = (size + THREAD_META_SIZE - 1) / PAGE_SIZE + 1;
	
	//go to the tail of the link
	pt_node *node = &page_table[block->page_table_index];
	while (node->next != NULL)
		node = node->next;

	for (size_t j = 0 ; j < num_of_pages ; j++)
	{
		void *address = (void*)((char*)(block + 1) + block->size);
		int i = 0;
		while (i < PAGE_TABLE_SIZE && page_table[i].current_addr != address)
			i++;

		//in this case, the target memory is in use; we need to move it out
		if (i != PAGE_TABLE_SIZE)
		{
			//swap the page in that memory out to a free space
			//must use peek(), not dequeue()
			int page_number = peek();
			void *target = (void*)((char*)memory_base + page_number * PAGE_SIZE);
			if (swap(address, target, i) == 0)
				return 0;
		}
		
		//unprotect the address which we will allocate to the thread
		mprotect(address, PAGE_SIZE, PROT_READ | PROT_WRITE);

		//insert the new page into the page table 
		int j = 0;
		while (j < PAGE_TABLE_SIZE && page_table[j].owner_id != -1)
			j++;
		if (j == PAGE_TABLE_SIZE)
			return 0;

		page_table[j].owner_id = current_thread_id;
		page_table[j].map_to_addr = address;
		page_table[j].current_addr = address;
		page_table[j].page_num = ((char*)address - (char*)memory_base) / PAGE_SIZE;
		page_table[j].next = NULL;

		while (page_table[j].page_num != peek())
			enqueue(dequeue());
		dequeue();
		
		//update page table size
		pt_size++;

		node->next = &page_table[j];
		node = node->next;

		block->size += PAGE_SIZE;
	}
	return 1;
}

//TODO: check correctness
int request_external(size_t size, block_meta *block)
{
	//calculate how many pages we need to allocate
	size_t num_of_pages = (size + THREAD_META_SIZE - 1) / PAGE_SIZE + 1;

	if ((PAGE_TABLE_SIZE - pt_size) < size)
		return 0;

	pt_node *node = &page_table[block->page_table_index];
	while (node->next != NULL)
		node = node->next;

	for (size_t j = 0 ; j < num_of_pages ; j++)
	{
		/*swap out pages inside and create new pages there*/
		void *address = (void*)((char*)(block + 1) + block->size);

		int i = 0;
		while (i < PAGE_TABLE_SIZE && page_table[i].current_addr == address)
			i++;
		if (i == PAGE_TABLE_SIZE)
		{
			//All physical memory is in use but no one has this address? Can't happen if everything runs correctly!!
			printf("BUG at request external!\n");
			return 0;
		}
		if (swap_out(page_table[i].current_addr, i) == 0)
			return 0;

		//As the memory is full and we just enqueue a page number by doing swap out
		//the page number we get from dequeue should be the one which we just enqueued
		int page_number = dequeue();
		
		//insert page table
		i = 0;
		while (i < PAGE_TABLE_SIZE && page_table[i].owner_id != -1)
			i++;
		if (i == PAGE_TABLE_SIZE)
			return 0;
		page_table[i].owner_id = current_thread_id;
		page_table[i].map_to_addr = address;
		page_table[i].current_addr = page_table[i].map_to_addr;
		page_table[i].page_num = page_number;
		page_table[i].next = NULL;

		//update page table size
		pt_size++;

		if (swap(page_table[i].current_addr, page_table[i].map_to_addr, i) == 0)
			return 0;

		node->next = &page_table[i];
		node = node->next;

		block->size += PAGE_SIZE;
	}
	return 1;
}

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
	if (queue_size() >= num_of_pages)
	{
		if (request_internal(size, block))
			return 1;
		else 
			return 0;
	}
	else
	{
		size_t intern_size = queue_size();
		size_t extern_size = num_of_pages - intern_size;
		if (request_internal(intern_size, block) && request_external(extern_size, block))
			return 1;
		else
			return 0;
	}
}

/*@return: the thread block meta if success; NULL if no space to allocate
 *@comment: may result in waste of memory if we request more memory at first
 *time
 * */
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
		
		//unprotect the free page which we are going to use
		mprotect(block, PAGE_SIZE, PROT_READ | PROT_WRITE);
		
		block->size = PAGE_SIZE - META_SIZE;
		block->owner_id = current_thread_id;

		//insert page table
		int i = 0;
		while (i < PAGE_TABLE_SIZE && page_table[i].owner_id != -1)
			i++;
		//This case shouldn't happen. If happens, need to fix bug
		if (i == PAGE_TABLE_SIZE)
		{
			printf("BUG: queue size > 0 while page table is full. error at malloc_lib\n");
			return NULL;
		}
		block->page_table_index = i;
		page_table[i].owner_id = current_thread_id;
		page_table[i].page_num = page_number;
		page_table[i].map_to_addr = memory_base;
		page_table[i].current_addr = block;
		page_table[i].isFirst = 1;
		
		//update page table size
		pt_size++;

		//swap the new memory to the first page and revise the block pointer
		if (swap(page_table[i].current_addr, page_table[i].map_to_addr, block->page_table_index) == 0)
			return NULL;
		block = page_table[i].map_to_addr;

		//initialize the thread block
		t_block= (tb_meta*)(block + 1);
		t_block->size = PAGE_SIZE - META_SIZE - THREAD_META_SIZE;
		t_block->isFree = 1;
	}
	else if (pt_size < PAGE_TABLE_SIZE)
	{
		//swap_out the first page at memory_base
		mprotect(memory_base, PAGE_SIZE, PROT_READ | PROT_WRITE);
		
		if (swap_out(memory_base, ((block_meta*)memory_base)->page_table_index) == 0)
			return NULL;
		//then write the page table info
		int i = 0;
		while (i < PAGE_TABLE_SIZE && page_table[i].owner_id != -1)
			i++;
		//This case shouldn't happen. If happens, need to fix bug
		if (i == PAGE_TABLE_SIZE)
		{
			printf("BUG: queue size > 0 while page table is full. error at malloc_lib\n");
			return NULL;
		}
		block = (block_meta*)memory_base;
		block->page_table_index = 0;
		block->owner_id = current_thread_id;
		block->size = PAGE_SIZE - META_SIZE;

		page_table[i].owner_id = current_thread_id;
		page_table[i].page_num = 0;
		page_table[i].map_to_addr = memory_base;
		page_table[i].current_addr = memory_base;
		page_table[i].isFirst = 1;

		//dirty...dirty...dirty
		while (peek() != 0)
			enqueue(dequeue());
		dequeue();
		
		//update page table size
		pt_size++;

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
	//find the first page of current thread
	block_meta *block;
	int i;
	for (i = 0 ; i < PAGE_TABLE_SIZE ; i++)
	{
		//find the page table who has the same id and isFirst part is 1
		if ((page_table[i].owner_id == current_thread_id) && (page_table[i].isFirst == 1))
		{
			//use the current address to locate the page_table_index, then swap, finally give the block map_to_addr
			if (page_table[i].isExtern == 0)
			{
				block = (block_meta *) page_table[i].current_addr;
				mprotect(block, PAGE_SIZE, PROT_READ | PROT_WRITE);
				swap(page_table[i].current_addr, page_table[i].map_to_addr, block->page_table_index);
			}
			else
			{
				//TODO: check correctness
				int j = 0;
				while (j < PAGE_TABLE_SIZE && page_table[j].current_addr != memory_base)
					j++;
				if (j == PAGE_TABLE_SIZE)
					swap_in(memory_base, i);
				else if (queue_size() == 0)
				{
					if (swap_out(memory_base, j) == 0)
						return NULL;
					swap_in(memory_base, i);
				}
				else
				{
					int page_number = peek();
					void *address = (void*)((char*)memory_base + page_number * PAGE_SIZE);
					
					if(swap(page_table[j].current_addr, address, j) == 0)
						return NULL;

					swap_in(memory_base, i);
				}
			}
			block = (block_meta *) page_table[i].map_to_addr;
			break;
		}
	}

	//check the allocated_size and find the free place to allocate memory
	tb_meta *t_block = find_free_space(size, block);
	return t_block;
}
 
void *myallocate(size_t size, char FILE[], int LINE, int type)
{
	//I don't want to give one thread too much memory
	//The maximum memory for a thread is the size of
	//whole physical memory
	if (size <= 0 || size >= MEMORY_SIZE)
	{
		return NULL;
	}

	//first call, initialize queue and page table
	if (memory_base == NULL)
	{
		init_queue();
		init_page_table();
		init_extern_table();
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
	else if (type == LIBRARYREQ)                      
	{		
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
	//	thread_mem_merge(t_block);
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
			node->isFirst = 0;
			node->map_to_addr = NULL;
			node->current_addr = NULL;
			enqueue(node->page_num);
			node->page_num = -1;

			//update: clear external table related data 
			if (node->isExtern == 1)
				extern_table[node->extern_index] = 0;
			node->isExtern = 0;
			node->extern_index = -1;

			if (node->next != NULL)
			{
				pt_node *temp = node;
				node = node->next;
				temp->next = NULL;
			}
			else
				node = node->next;
			//update page table size
			pt_size--;
		}
	}
	else
		printf("Wrong request type!\n");
}

int main()
{
/*	int thread_id_1 = 1;
	int thread_id_2 = 2;
    int thread_id_3 = 3;

	current_thread_id = 1;
	void *thread_1 = myallocate(4000, NULL, 0, LIBRARYREQ);
	current_thread_id = 2;
	void *thread_2 = myallocate(4000, NULL, 0, LIBRARYREQ);

	printf("thread 1 address: %x\n", thread_1);
	printf("thread 2 address: %x\n", thread_2);

	current_thread_id = 2;
	void *t1_proc1 = myallocate(4055, NULL, 0, THREADREQ);
	printf("thread 2 threadrequest success.\n");
	current_thread_id = 1;
	mydeallocate(t1_proc1, NULL, 0, THREADREQ);
	void *t1_proc2 = myallocate(1, NULL, 0, THREADREQ);

//	current_thread_id = 2;
//	mydeallocate(thread_2, NULL, 0, LIBRARYREQ);
//	current_thread_id = 3;
//	void *thread_3 = myallocate(4000, NULL, 0, LIBRARYREQ);

	printf("thread 1 proc 1 address: %x\n", t1_proc1);
	printf("thread 1 proc 2 address: %x\n", t1_proc2);
*/
	int thread_id_1 = 1;
	int thread_id_2 = 2;
	int thread_id_3 = 3;

	int j = 0;
	current_thread_id = 1;
	void *thread_1 = myallocate(4000, NULL, 0, LIBRARYREQ);
	for(j = 0; j < 5;j++){
		*((int *)thread_1 + j) = 10;
		printf("thread1_%dth row context: %x\n",j,*((int *)thread_1 + j));

	}
	printf("**************\n");

	current_thread_id = 2;
	void *thread_2 = myallocate(4000, NULL, 0, LIBRARYREQ);

    void *t1_proc1 = myallocate(MEMORY_SIZE - 20000, NULL, 0, THREADREQ);

	int i;
	for(i = 0; i < PAGE_SIZE;i++){
		if(page_table[i].owner_id != -1){
			printf("owner_id = %d\n",page_table[i].owner_id);
		}
	}
	current_thread_id = 1;
								
	printf("**************\n");
	for(j = 0; j < 5;j++)
		printf("thread1_%dth row context: %x\n",j,*((int *)thread_1 + j));

	return 0;
}
