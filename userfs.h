#include <stddef.h>
#include <sys/types.h>


/** User-defined in-memory filesystem. It is as simple as possible. Each file
 * lies in the memory as an array of blocks. A file has a unique file name, and
 * there are no directories, so the FS is a monolite flat contiguous folder. */

enum open_flags {
	/** If the flag is specified and file does not exist - create it. */
	UFS_CREATE = 1,
};

/** Possible errors from all functions. */
enum ufs_error_code {
	UFS_ERR_NO_ERR = 0,
	UFS_ERR_NO_FILE,
	UFS_ERR_NO_MEM,
	UFS_ERR_READING_EMPTY_FILE, // ?
};

/** Get code of the last error. */
enum ufs_error_code ufs_errno();

/** Returns fd or -1 if error occured. Check ufs_errno() for a code.
 * - UFS_ERR_NO_FILE - no such file, and UFS_CREATE flag is not specified. */
int ufs_open(const char *filename, int flags);

/** Params: file descriptor, destination buffer, buffer size.
 * Return value > 0 - number of bytes written; -1 - error occured. 
 * Check ufs_errno() for a code: - UFS_ERR_NO_FILE - invalid file descriptor.
 *   							 - UFS_ERR_NO_MEM - not enough memory. */
ssize_t ufs_write(int fd, const char *buf, size_t size);

/** size - max bytes to read.
 * Return value > 0 number of bytes read; 0 EOF; -1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid fd. */
ssize_t ufs_read(int fd, char *buf, size_t size);

/** Return value 0 Success;-1 Error occured. 
 * Check ufs_errno() for a code. - UFS_ERR_NO_FILE - invalid file descriptor. */
int ufs_close(int fd);

/** Returns -1 if error occured. Check ufs_errno() for a code.
 * - UFS_ERR_NO_FILE - no such file. */
int ufs_delete(const char *filename);