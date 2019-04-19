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

int has_create_flag(int flags) {
	if (!(flags & UFS_CREATE)) {
		ufs_errn = UFS_ERR_NO_FILE;
		return 0;
	}
	return 1;
}

/** Returns fd or -1 if error occured. Check ufs_errno() for a code.
 * - UFS_ERR_NO_FILE - no such file, and UFS_CREATE flag is not specified. */
int ufs_open(const char *filename, int flags) {
	if (!file_list && !has_create_flag(flags)) {
		return -1;
	}	// ufs_open("file", 0) == -1,

	struct file *current_file = file_list;
	while (current_file) {
		if (!strcmp(current_file->name, filename)) 
			goto manage_fd;
		current_file = current_file->next;
	}

	if (!has_create_flag(flags))
		return -1;

	current_file = calloc(1, sizeof(struct file));
	current_file->name = filename;

	if (file_list) {
		last_file->next = current_file;
		current_file->prev = last_file;
	} else {
		file_list = current_file;
	}

	last_file = current_file;

manage_fd:
	++current_file->refs;
	for (int i = 0; i < fd_count; ++i) {
		if (fd_array[i] == NULL) {
			struct fdesc *fd = calloc(1, sizeof(struct fdesc));
			fd->file = current_file;
			fd_array[i] = fd;
			return i;
		}
	}
	if (fd_count >= fd_capacity) {
		fd_array = realloc(fd_array, (fd_capacity + 1) * 2 * sizeof(struct fdesc));
		fd_capacity = (fd_capacity + 1) * 2;
	}

	struct fdesc *fd = calloc(1, sizeof(struct fdesc));
	fd->file = current_file;
	fd_array[fd_count] = fd;

	return fd_count++;
}

struct block* next_block(struct block* previous_block) {
	struct block* next_block = previous_block->next;
	if (!next_block) {
		next_block = calloc(1, sizeof(struct block));
		next_block->memory = malloc(BLOCK_SIZE);
		previous_block->next = next_block;
		next_block->prev = previous_block;
	}
	return next_block;
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

	if (size <= 0) 
		return 0;

	struct file *f = fd_array[fd]->file;

	if (!f->block_list) {
		f->block_list = calloc(1, sizeof(struct block));
		f->block_list->memory = malloc(BLOCK_SIZE);
	}

	struct block *current_block = f->block_list;
	int i = 0;
	for (i = 0; i < fd_array[fd]->block_num; ++i) {
		current_block = next_block(current_block);
	}

	size_t bytes_written = 0;
	if (fd_array[fd]->offset) {
		int n = BLOCK_SIZE - fd_array[fd]->offset >= size ? 
			size : BLOCK_SIZE - fd_array[fd]->offset;
		memcpy(current_block->memory + fd_array[fd]->offset, buf, n);
		bytes_written += n;
		size -= n;
		buf += n;
		if (size) {
			++i;
			current_block = next_block(current_block);
		}
	}

	while (size) {
		if (size <= BLOCK_SIZE) {
			memcpy(current_block->memory, buf, size);
			bytes_written += size;
			fd[fd_array]->offset = size;
			break;
		}
		memcpy(current_block->memory, buf, BLOCK_SIZE);
		bytes_written += BLOCK_SIZE;
		size -= BLOCK_SIZE;
		buf += BLOCK_SIZE;

		current_block = next_block(current_block);
		++i;
	}

	fd[fd_array]->block_num = i;
	return bytes_written;
}

/** size - max bytes to read.
 * Return value > 0 number of bytes read; 0 EOF; -1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid fd. */
ssize_t ufs_read(int fd, char *buf, size_t size) {
	if (fd >= fd_count || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} else if (size <= 0) {
		return 0;
	}

	struct file *f = fd_array[fd]->file;
	struct block *current_block = f->block_list;
	int i = 0;
	for (i = 0; i < fd_array[fd]->block_num; ++i) {
		current_block = current_block->next;
	}

	int bytes_readed = 0;
	if (fd_array[fd]->offset) {
		int n = BLOCK_SIZE - fd_array[fd]->offset >= size ? 
			size : BLOCK_SIZE - fd_array[fd]->offset;
		memcpy(buf, current_block->memory + fd_array[fd]->offset, n);
		bytes_readed += n;
		size -= n;
		buf += n;
		if (size) {
			++i;
			current_block = current_block->next;
		}
	}

	while (current_block && size) {
		if (size <= BLOCK_SIZE) {
			memcpy(buf, current_block->memory, size);
			bytes_readed += size;
			fd_array[fd]->offset = size;
			break;
		}
		memcpy(buf, current_block->memory, BLOCK_SIZE);
		buf += BLOCK_SIZE;
		bytes_readed += BLOCK_SIZE;
		size -= BLOCK_SIZE;
		current_block = current_block->next;
		++i;

		if (i == MAX_FILE_SIZE/BLOCK_SIZE && size > BLOCK_SIZE) {
			ufs_errn = UFS_ERR_NO_MEM;
			return -1;
		}
	}

	fd_array[fd]->block_num = i;

	return bytes_readed;
}

/** Return value 0 Success;-1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid file descriptor.*/
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
			
			for (int i = 0; i < fd_count; ++i) {
				if (!strcmp(fd_array[i]->file->name, filename)) {
					free(fd_array[i]);
					fd_array[i] = NULL;
				}
				
			}
			for (int j = fd_count; j > 0; --j) {
				if (!fd_array[j]) 
					--fd_count;
				else
					break;
			}

			free(current_file);
			return 0;
		}
		current_file = current_file->next;
	}

	ufs_errn = UFS_ERR_NO_FILE;

	return -1;
}