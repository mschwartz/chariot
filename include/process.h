#pragma once

#include <fs.h>
#include <func.h>
#include <lock.h>
#include <sched.h>
#include <string.h>
#include <syscalls.h>
#include <stat.h>
#include <errno.h>
#include <fs/vfs.h>
#include <dirent.h>

#define RING_KERNEL 0
#define RING_USER 3

void syscall_init(void);
long ksyscall(long n, ...);

#define SYSSYM(name) sys_##name



#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_NOCTTY 0400
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_NONBLOCK 04000
#define O_DIRECTORY 00200000
#define O_NOFOLLOW 00400000
#define O_CLOEXEC 02000000
#define O_NOFOLLOW_NOERROR 0x4000000

namespace sys {
#include <syscall.def.h>
}  // namespace sys
