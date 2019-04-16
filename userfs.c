#include "userfs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

enum {
	BLOCK_SIZE = 512,
	MAX_FILE_SIZE = 1024 * 1024 * 1024,
};

/** Global error code. Set from any function on any error. */
static enum ufs_error_code ufs_errn = UFS_ERR_NO_ERR;

struct block {
	char *memory;
	int occupied; // in bytes
	struct block *next;
	struct block *prev;
};

struct file {
	struct block *block_list;
	struct block *last_block;
	int refs; // number of fds opened on file
	const char *name;
	struct file *next;
	struct file *prev;
};

static struct file *file_list = NULL;
static struct file *last_file = NULL;

struct fdesc {
	struct file *file;
	int block_num; // where user left off
	int offset; // and more precisely
};

/** An array of file descriptors. When a file descriptor is closed, its place
 * in this array is set to NULL and can be taken by next ufs_open() call. */
static struct fdesc **fd_array = NULL;
static int fd_count = 0;
static int fd_capacity = 0;

enum ufs_error_code ufs_errno() {
	return ufs_errn;
} 

int check_flags(int flags) {
	if (!(flags & UFS_CREATE)) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	}
	return 0;
}

/** Returns fd or -1 if error occured. Check ufs_errno() for a code.
 * - UFS_ERR_NO_FILE - no such file, and UFS_CREATE flag is not specified. */
int ufs_open(const char *filename, int flags) {
	printf("Yay\n");
	if (!file_list && check_flags(flags)) {
		return -1;
		printf("Check\n");
	}

	struct file *current_file = file_list;
	while (current_file) {
		if (!strcmp(current_file->name, filename)) 
			goto manage_fd;
		current_file = current_file->next;
	}

	printf("One\n");

	if (check_flags(flags))
		return -1;

	current_file = malloc(sizeof(struct file));
	memset(current_file, 0, sizeof(struct file)); 

	if (file_list) {
		last_file->next = current_file;
		current_file->prev = last_file;
	} else {
		file_list = current_file;
	}

	last_file = current_file;

manage_fd:
	printf("Checkkity\n");
	++current_file->refs;
	for (int i = 0; i < fd_count; ++i) {
		if (fd_array[i] == NULL) {
			struct fdesc *fd = malloc(sizeof(struct fdesc));
			fd->file = current_file;
			fd_array[i] = fd;
			return i;
		}
	}
	if (fd_count >= fd_capacity) {
		fd_array = realloc(fd_array, (fd_capacity + 1) * 2 * sizeof(struct fdesc));
		fd_capacity = (fd_capacity + 1) * 2;
	}

	printf("Two\n");

	struct fdesc *fd = malloc(sizeof(struct fdesc));
	fd->file = current_file;
	fd_array[fd_count] = fd;

	return fd_count++;
}

/** Params: file descriptor, destination buffer, buffer size.
 * Return value > 0 - number of bytes written; -1 - error occured. 
 * Check ufs_errno() for a code: - UFS_ERR_NO_FILE - invalid file descriptor.
 *   							 - UFS_ERR_NO_MEM - not enough memory. */
ssize_t ufs_write(int fd, const char *buf, size_t size) {
	if (fd >= fd_count || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} 

	printf("Three\n");

	if (size <= 0) 
		return 0;

	struct file *f = fd_array[fd]->file;
	struct block *current_block;

	if (fd_array[fd]->block_num) {
		current_block = f->block_list;
		for (int i = 0; i < fd_array[fd]->block_num; ++i) {
			current_block = current_block->next;
		}
	} else {
		current_block = malloc(sizeof(struct block)); 
		f->block_list = current_block;
		f->last_block = current_block;
		fd_array[fd]->block_num = 1;
	}
	
	size_t bytes_written = 0;
	if (fd_array[fd]->offset) {
		int n = BLOCK_SIZE - fd_array[fd]->offset;
		memcpy(current_block + fd_array[fd]->offset, buf, n);
		bytes_written += size - n;
		size -= n;
		buf += n;
	}

	if (size < 0) 
		return bytes_written;

	struct block *next_block = malloc(sizeof(struct block));
	while (size) {
		++fd_array[fd]->block_num;
		current_block->next = next_block;
		next_block->prev = current_block;
		current_block = next_block;

		if (size < BLOCK_SIZE) {
			memcpy(current_block, buf, size);
			bytes_written += size;
			break;
		}
		// ne v block pishem a v ego memory, ee tozhe malloc
		memcpy(current_block, buf, BLOCK_SIZE);
		fd_array[fd]->offset = abs(size - BLOCK_SIZE); 
		bytes_written += size - BLOCK_SIZE;
		size -= BLOCK_SIZE;
		buf += BLOCK_SIZE;

		if (size > 0) {
			next_block = malloc(sizeof(struct block)); 
		}
	}

	return bytes_written;
} // NO MEM ERR zayuzat'

/** size - max bytes to read.
 * Return value > 0 number of bytes read; 0 EOF; -1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid fd. */
ssize_t ufs_read(int fd, char *buf, size_t size) {
	if (fd >= fd_count || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} else if (!fd_array[fd]->block_num) {
		ufs_errn = UFS_ERR_READING_EMPTY_FILE;
		return -1;
	} else if (size <= 0) {
		return 0;
	}

	struct file *f = fd_array[fd]->file;
	while (size) {
		if (fd_array[fd]->block_num) {
			struct block *current_block = f->block_list;
			for (int i = 0; i < fd_array[fd]->block_num; ++i) {
				current_block = current_block->next;
			}
		}
	}

}

/** Return value 0 Success;-1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid file descriptor. */
int ufs_close(int fd) {
	if (fd >= fd_count || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} 

	free(fd_array[fd]);
	fd_array[fd] = NULL;

	while (fd_count > 0 && fd_array[fd_count - 1] == NULL) {
		--fd_count;
	}

	return 0;
}

/** Returns -1 if error occured. Check ufs_errno() for a code.
 * - UFS_ERR_NO_FILE - no such file. */
int ufs_delete(const char *filename) {
	struct file *current_file = file_list;

	while (current_file) {
		if (!strcmp(current_file->name, filename)) {
			if (current_file != file_list)
				current_file->prev->next = current_file->next;
			if (current_file != last_file)	
				current_file->next->prev = current_file->prev;

			struct block *b = current_file->block_list, *next_block;
			while (b) {
				next_block = b->next;
				free(b->memory);
				free(b);
				b = next_block;
			}

			free(current_file);
			return 0;
		}
		current_file = current_file->next;
	}

	ufs_errn = UFS_ERR_NO_FILE;

	return -1;
}