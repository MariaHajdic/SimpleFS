#include "userfs.h"
#include <stdio.h>

int main() {
	const char *f1, *f2, *f3;
	int flgs = 0;

	int fd1 = ufs_open(f1, flgs);
	int fd2 = ufs_open(f2, flgs);
	int fd3 = ufs_open(f3, flgs);

	const char *b1 = "Testcase one";
	const char *b2 = "Testcase one";
	const char *b3 = "Testcase one";

	printf("Here\n");

	ufs_write(fd1, b1, sizeof(b1));

	printf("There\n");

	char *read1; 
	ufs_read(fd1, read1, sizeof(read1));

	printf("%s\n", read1);

	return 0;
}