#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdbool.h>
#include "threads/thread.h"
#include "include/lib/user/syscall.h"

void syscall_init (void);

void check_address (void *addr);
void halt(void);
void exit(int status);
struct file* get_file(int fd);
int write(int fd, const void *buffer, unsigned size);
bool remove (const char *file);
bool create (const char *file, unsigned initial_size);
int open (const char *file);
int read (int fd, void *buffer, unsigned length);
void remove_fd(int fd);
void close(int fd);
int filesize (int fd);
void seek (int fd, unsigned position);
unsigned tell (int fd);
pid_t fork(const char *thread_name);
int exec(const char *file_name);
int wait (tid_t pid);


#endif /* userprog/syscall.h */
