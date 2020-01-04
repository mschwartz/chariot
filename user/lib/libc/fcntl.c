#include <fcntl.h>
#include <sys/syscall.h>
#include <stdarg.h>



// TODO
int creat(const char *pth, mode_t mod) {
  return -1;
}


// TODO
int fcntl(int fd, int cmd, ...) {
  return -1;
}

int open(const char *filename, int flags, ...) {

	mode_t mode = 0;

	if ((flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE) {
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, mode_t);
		va_end(ap);
	}

	int fd = syscall(SYS_open, filename, flags, mode);
	// if (fd>=0 && (flags & O_CLOEXEC)) {
	//  __syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);
  // }

	return fd; // __syscall_ret(fd);
}
