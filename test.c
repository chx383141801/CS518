#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#define PAGE sysconf(_SC_PAGE_SIZE)
#define SIZE 1024*1024*8/PAGE

char c[100];

int main()
{/*
	long size = SIZE;//sysconf(_SC_PAGE_SIZE);
	c[0] = 'a';
	int *a = (int*)(c+1);
	printf("%x, %x, %x\n", c, c+1, a+1);*/
	FILE *fp = fopen("test.txt", "rb+");
	//fputs("aaaaabbbbbccccc", fp);
	fseek(fp, 5, SEEK_SET);
	fputs("BBBBB", fp);
	return 0;
}
