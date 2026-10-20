/* Darwin runtime capability, not a claim that App Store apps normally fork. */
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
int main(void) {
    volatile int *memory=mmap(0,16384,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    if (memory==MAP_FAILED) return 31;
    *memory=11; pid_t child=fork();
    if (!child) { *memory=99; _exit(37); }
    int status=0;
    if (child<0 || waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status)!=37 || *memory!=11) return 32;
    if (munmap((void *)memory,16384)) return 33;
    puts("Process COW: 11 child: 37"); fflush(stdout);
    return getchar()=='q' ? 0 : 34;
}
