#include "userfs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

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
	char *name;
	int size; // in bytes
	int refs; // number of fds opened on file
	bool is_deleted;
	struct block *block_list;
	struct block *last_block;
	struct file *next;
	struct file *prev;
};

static struct file *file_list = NULL;
static struct file *last_file = NULL;

struct fdesc { 
	struct file *file;
	struct block *block_position;
	int block_num; // where user left off
	int offset; // and more precisely
	int flags;
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
	int i = 0;
	if (!file_list && !has_create_flag(flags)) {
		return -1;
	}	

	struct file *current_file = file_list;
	while (current_file) {
		if (!strcmp(current_file->name, filename)) 
			goto manage_fd;
		current_file = current_file->next;
	}

	if (!has_create_flag(flags))
		return -1;

	current_file = calloc(1, sizeof(struct file));
	current_file->name = malloc(strlen(filename));
	current_file->is_deleted = false;
	memcpy(current_file->name, filename, strlen(filename) + 1);

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
			fd->flags = ( (flags <= 1) ? UFS_READ_WRITE : flags );
			fd_array[i] = fd;
			return i;
		}
	}
	
	if (fd_count >= fd_capacity) {
		fd_array = realloc(fd_array, (fd_capacity+1) * 2*sizeof(struct fdesc));
		fd_capacity = (fd_capacity + 1) * 2;
	}

	struct fdesc *fd = calloc(1, sizeof(struct fdesc));
	fd->file = current_file;
	fd->flags = ( (flags <= 1) ? UFS_READ_WRITE : flags );
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
	if (fd >= fd_count || fd < 0 || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	}
	if (fd_array[fd]->file->size + size > MAX_FILE_SIZE) {
		ufs_errn = UFS_ERR_NO_MEM;
		return -1;
	}
	if (!(fd_array[fd]->flags & ( UFS_WRITE_ONLY | UFS_READ_WRITE ) )) {
		ufs_errn = UFS_ERR_NO_PERMISSION;
		return -1;
	}
	if (size <= 0) 
		return 0;

	struct file *f = fd_array[fd]->file;

	if (!f->block_list) {
		f->block_list = calloc(1, sizeof(struct block));
		f->block_list->memory = malloc(BLOCK_SIZE);
	}
	
	struct block *current_block = ( fd_array[fd]->block_position ? 
		fd_array[fd]->block_position : f->block_list );
	int i = ( fd_array[fd]->block_num ? fd_array[fd]->block_num : 0 );

	size_t bytes_written = 0;
	if (fd_array[fd]->offset) {
		int n = BLOCK_SIZE - fd_array[fd]->offset >= size ? 
			size : BLOCK_SIZE - fd_array[fd]->offset;
		if ( (i == MAX_FILE_SIZE / BLOCK_SIZE - 1) && 
			(size > BLOCK_SIZE - fd_array[fd]->offset)) {
			ufs_errn = UFS_ERR_NO_MEM;
			return -1;
		}
		memcpy(current_block->memory + fd_array[fd]->offset, buf, n);
		bytes_written += n;
		size -= n;
		buf += n;
		current_block->occupied += ( (fd_array[fd]->offset + n < 
			current_block->occupied) ? 0 : n - current_block->occupied +
			fd_array[fd]->offset );

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
			current_block->occupied = size;
			break;
		}
		memcpy(current_block->memory, buf, BLOCK_SIZE);
		bytes_written += BLOCK_SIZE;
		size -= BLOCK_SIZE;
		buf += BLOCK_SIZE;
		current_block->occupied += BLOCK_SIZE;
		current_block = next_block(current_block);
		++i;
	}

	fd[fd_array]->block_num = i;
	fd_array[fd]->block_position = current_block;
	return bytes_written;
}

/** size - max bytes to read.
 * Return value > 0 number of bytes read; 0 EOF; -1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid fd. */
ssize_t ufs_read(int fd, char *buf, size_t size) {
	if (fd >= fd_count || fd < 0 || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} else if (size <= 0) {
		return 0;
	}

	if (!(fd_array[fd]->flags & ( UFS_READ_ONLY | UFS_READ_WRITE ) )) {
		ufs_errn = UFS_ERR_NO_PERMISSION;
		return -1;
	}

	struct file *f = fd_array[fd]->file;
	struct block *current_block = ( fd_array[fd]->block_position ? 
		fd_array[fd]->block_position : f->block_list );
	int i = ( fd_array[fd]->block_num ? fd_array[fd]->block_num : 0 );
	
	int bytes_read = 0, curr_read = 0;
	if (fd_array[fd]->offset) {
		int n, offset = fd_array[fd]->offset;

		if (BLOCK_SIZE - fd_array[fd]->offset >= size) {
			n = size;
			fd_array[fd]->offset += n; 
		} else {
			n = BLOCK_SIZE - fd_array[fd]->offset;
			fd_array[fd]->offset = 0;
		}

		curr_read = ( (current_block->occupied - offset > n) ? n : 
			current_block->occupied - offset );
		memcpy(buf, current_block->memory + offset, curr_read);
		bytes_read += curr_read;
		size -= n;
		buf += n;

		if (size) {
			++i;
			current_block = current_block->next;
		}
	}

	while (current_block && size) {
		if (size <= BLOCK_SIZE) {
			curr_read = ( (current_block->occupied < size) ? 
				current_block->occupied : size );
			bytes_read += curr_read;
			memcpy(buf, current_block->memory, curr_read);
			fd_array[fd]->offset = curr_read;
			break;
		}
		curr_read = ( (current_block->occupied < size) ? 
			current_block->occupied : BLOCK_SIZE );
		memcpy(buf, current_block->memory, curr_read);
		bytes_read += curr_read;
		size -= BLOCK_SIZE;
		buf += BLOCK_SIZE;
		current_block = current_block->next;
		++i;
	}

	fd_array[fd]->block_num = i;
	fd_array[fd]->block_position = current_block;
	return bytes_read;
}

int ufs_close(int fd) {
	if (fd >= fd_count || fd < 0 || fd_array[fd] == NULL) {
		ufs_errn = UFS_ERR_NO_FILE;
		return -1;
	} 

	if (fd_array[fd]->file->is_deleted && !--fd_array[fd]->file->refs)
		ufs_delete(fd_array[fd]->file->name);

	free(fd_array[fd]);
	fd_array[fd] = NULL;

	while (fd_count > 0 && fd_array[fd_count - 1] == NULL) {
		--fd_count;
	}
	
	return 0;
}

int ufs_delete(const char *filename) {
	struct file *current_file = file_list;

	while (current_file) {
		if (!strcmp(current_file->name, filename)) {
			current_file->is_deleted = true;
			if (current_file->refs > 0) 
				return 0;

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
			
			if (current_file == file_list) {
				file_list = ((current_file->next) ? current_file->next : NULL);
			}
			free(current_file->name);
			free(current_file);
			current_file = NULL;

			return 0;
		}
		current_file = current_file->next;
	}
	ufs_errn = UFS_ERR_NO_FILE;

	return -1;
}