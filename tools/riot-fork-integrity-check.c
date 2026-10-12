#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <elfcalls.h>
extern struct elf_calls *_elfcalls;
int main(void) {
    void *lib=_elfcalls->dlopen("@PROJECT_ROOT@/build/libriot-host-fork-stress.so");
    if(!lib)return 2;
    void (*start)(void)=_elfcalls->dlsym(lib,"fork_stress_start");
    void (*stop)(void)=_elfcalls->dlsym(lib,"fork_stress_stop");
    void (*child)(void)=_elfcalls->dlsym(lib,"fork_stress_child");
    if(!start||!stop||!child)return 3;
    start();int passed=0;
    for(int i=0;i<64;i++) {
        pid_t pid=fork();if(pid<0)break;
        if(!pid){child();_exit(0);}
        int status=0,done=0;
        for(int j=0;j<500;j++){if(waitpid(pid,&status,WNOHANG)==pid){done=1;break;}usleep(10000);}
        if(!done){kill(pid,SIGKILL);waitpid(pid,&status,0);break;}
        if(!WIFEXITED(status)||WEXITSTATUS(status))break;
        passed++;
    }
    stop();printf("FORK_INTEGRITY passed=%d/64 concurrent_host_allocators=6\n",passed);
    return passed==64?0:1;
}
