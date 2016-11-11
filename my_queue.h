#ifndef MY_QUEUE_H
#define MY_QUEUE_H
#include <stdio.h>
#include <stdlib.h>

int dequeue();
int is_empty();
int peek();

void creat_queue();
void enqueue(int data);
void print_queue();
void init_queue();

size_t queue_size();

#endif
