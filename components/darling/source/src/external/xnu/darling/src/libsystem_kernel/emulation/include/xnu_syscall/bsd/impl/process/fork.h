#ifndef LINUX_FORK_H
#define LINUX_FORK_H

long sys_fork(void);

#if defined(__arm64__) || defined(__aarch64__)
void sys_fork_postfork_child(void);
#endif

#endif // LINUX_FORK_H
