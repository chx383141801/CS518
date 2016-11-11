#include "my_queue.h"

struct node
{
	int page_number;
	struct node *next;
}*front, *rear, *temp, *front1;

size_t size = 0;

void create_queue()
{
	front = rear = NULL;
}

size_t queue_size()
{
	return size;
}

void enqueue(int data)
{
	if (rear == NULL)
	{
		rear = (struct node *)malloc(sizeof(struct node));
		rear->next = NULL;
		rear->page_number = data;
		front = rear;
	}
	else
	{
		temp = (struct node *) malloc(sizeof(struct node));
		rear->next = temp;
		temp->page_number = data;
		temp->next = NULL;
		rear = temp;
	}
	size++;
}

/* Description: Dequeue the front element
 * @return: page_number in front; -1 on fail
 * */
int dequeue()
{
	front1 = front;
	int ret = 0;
	if (front1 == NULL)
	{
		printf("\n Error: Trying to dequeue an empty queue");
		return -1;
	}
	else
	{
		if (front1->next != NULL)
		{
			front1 = front1->next;
			ret = front->page_number;
			free(front);
			front = front1;
		}
		else
		{
			ret = front->page_number;
			free(front);
			front = NULL;
			rear = NULL;
		}
	}
	size--;
	return ret;
}

/* Description: Display elements in the queue
 * DEBUG USE
 * */
void print_queue()
{
	front1 = front;
	int counter = 0;
	if ((front1 == NULL) && (rear == NULL))
	{
		printf("Queue is empty\n");
		return;
	}
	while (front1 != rear)
	{
		printf("%d, ", front1->page_number);
		front1 = front1->next;
		counter = ++counter % 20;
		if (counter == 0)
			printf("\n");
	}
	if (front1 == rear)
		printf("%d\n", front1->page_number);
}


/* Description: Return the front element of queue
 * @return: info of front is queue is not empty; 0 if nothing in queue
 */
int peek()
{
	if ((front != NULL) && (rear != NULL))
		return front->page_number;
	else
		return 0;
}

/* @return: 1 is empty; 0 is not empty*/
int is_empty()
{
	if ((front == NULL) && (rear == NULL))
		return 1;
	else
		return 0;
}

/* Description: Initialize the free memory queue; Only for this assignment*/
void init_queue()
{
	create_queue();
	for (int i = 0 ; i < 2048 ; i++)
		enqueue(i);
}

/*
int main()
{
	init_queue();
	//print_queue();
	printf("%d\n", size);
	dequeue();
	printf ("%d\n", peek());
	printf("%d\n", size);
    return 0;
}*/
