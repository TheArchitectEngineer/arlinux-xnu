/* Real libSystem, interactive stdin and process return. */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
static void *compute(void *value) { *(int *)value=42; return (void *)37; }
int main(void) {
    int *value=malloc(32768); if (!value) return 71;
    *value=0; pthread_t thread; void *reply=0;
    if (pthread_create(&thread,0,compute,value) || pthread_join(thread,&reply)) return 72;
    if (*value!=42 || reply!=(void *)37) return 73;
    free(value); puts("Console ready"); fflush(stdout);
    unsigned count=0; int c;
    while ((c=getchar())!=EOF) {
        if (c=='+') { printf("Counter: %u\n",++count); fflush(stdout); }
        if (c=='q') { puts("Goodbye"); return count==1 ? 0 : 74; }
    }
    return 75;
}
