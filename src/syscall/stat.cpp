#include <cpu.h>
#include <process.h>
#include <stat.h>

int sys::stat(const char *pathname, struct stat *statbuf) { return -1; }

int sys::fstat(int fd, struct stat *statbuf) {
  auto proc = cpu::proc().get();

  auto file = proc->get_fd(fd);

  if (!file) return -ENOENT;

  // defer to the file's inode
  if (file->ino) return file->ino->stat(statbuf);
  return -1;
}

int sys::lstat(const char *pathname, struct stat *statbuf) { return -1; }
