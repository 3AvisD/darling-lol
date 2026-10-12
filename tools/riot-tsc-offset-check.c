#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
static uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC_RAW,&t); return t.tv_sec*1000000000ULL+t.tv_nsec; }
struct sample { uint64_t tsc, clock, gap; };
static struct sample sample(void) {
 struct sample best={0,0,UINT64_MAX};
 for(int i=0;i<100;++i) {
  uint64_t a=now(); unsigned aux; uint64_t t=__rdtscp(&aux); uint64_t b=now();
  if(b-a<best.gap) best=(struct sample){t,a+(b-a)/2,b-a};
 }
 return best;
}
int main(void) {
 cpu_set_t original,set;
 if(sched_getaffinity(0,sizeof(original),&original)) return 1;
 int first=-1;
 for(int i=0;i<CPU_SETSIZE;++i) if(CPU_ISSET(i,&original)){first=i;break;}
 if(first<0) return 2;
 CPU_ZERO(&set);CPU_SET(first,&set);if(sched_setaffinity(0,sizeof(set),&set))return 3;
 struct sample a=sample();struct timespec delay={0,100000000};nanosleep(&delay,0);struct sample b=sample();
 double cycles_ns=(double)(b.tsc-a.tsc)/(b.clock-a.clock);
 printf("TSC measured rate %.3f MHz; diagnostic process affinity only\n",cycles_ns*1000);
 struct sample reference=sample();
 for(int cpu=0;cpu<CPU_SETSIZE;++cpu) if(CPU_ISSET(cpu,&original)) {
  CPU_ZERO(&set);CPU_SET(cpu,&set);if(sched_setaffinity(0,sizeof(set),&set))return 4;
  struct sample c=sample();double offset=((double)c.tsc-reference.tsc)/cycles_ns-((double)c.clock-reference.clock);
  printf("cpu %d offset %.3f us bracket %.3f us\n",cpu,offset/1000,c.gap/1000.0);
 }
 return sched_setaffinity(0,sizeof(original),&original)!=0;
}
