/* Genuine dynamically linked Darwin console journey. These are the ordinary
 * LP64 Darwin interfaces; implementations come only from the real libSystem.
 * No SDK, Linux syscall, replacement dylib or host function is linked here. */
typedef unsigned long size_t;
typedef long ssize_t;
typedef void *pthread_t;
extern ssize_t write(int,const void *,size_t);
extern ssize_t read(int,void *,size_t);
extern void *malloc(size_t);
extern void free(void *);
extern int pthread_create(pthread_t *,const void *,void *(*)(void *),void *);
extern int pthread_join(pthread_t,void **);

static int say(const char *text,size_t length)
{
    while (length) {
        ssize_t count=write(1,text,length);
        if (count<=0) return 1;
        text+=count; length-=count;
    }
    return 0;
}
#define SAY(text) say(text,sizeof(text)-1)
struct payload { unsigned long first,second; };
static void *worker(void *argument)
{
    struct payload *data=argument;
    data->second=data->first+1;
    return (void *)37;
}
int main(void)
{
    if (SAY("DYNAMIC_MAIN real libSystem entered\n")) return 70;
    struct payload *data=malloc(32768);
    if (!data) { SAY("DYNAMIC_FAILURE malloc\n"); return 71; }
    data->first=41; data->second=0;
    pthread_t thread;
    int error=pthread_create(&thread,0,worker,data);
    if (error) { SAY("DYNAMIC_FAILURE pthread_create\n"); free(data); return 72; }
    void *value=0;
    error=pthread_join(thread,&value);
    if (error || value!=(void *)37 || data->second!=42) {
        SAY("DYNAMIC_FAILURE pthread_join/result\n"); free(data); return 73;
    }
    if (SAY("DYNAMIC_THREAD joined value=37 memory=42\n")) return 74;
    unsigned int counter=0;
    for (;;) {
        char action;
        if (read(0,&action,1)!=1) { free(data); return 75; }
        if (action=='+') counter++;
        if (action=='q') break;
    }
    free(data);
    if (counter!=1) return 76;
    if (SAY("DYNAMIC_CONSOLE counter=1 normal return\n")) return 77;
    return 0; /* Genuine libdyld/libSystem exit glue, not a raw host exit. */
}
