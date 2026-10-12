#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <mach/mach_time.h>
extern int _platform_strcmp(const char*,const char*);
extern int _platform_strncmp(const char*,const char*,size_t);
extern size_t _platform_strlen(const char*);
extern size_t _platform_strnlen(const char*,size_t);
extern int _platform_memcmp(const void*,const void*,size_t);
extern void* _platform_memmove(void*,const void*,size_t);
extern void* _platform_memchr(const void*,int,size_t);
static int sign(int v){return (v>0)-(v<0);}
static void check(int v,const char* message){if(!v){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
static int compare(const unsigned char*a,const unsigned char*b,size_t n){for(size_t i=0;i<n;i++)if(a[i]!=b[i])return a[i]-b[i];return 0;}
int main(void){
 size_t page=getpagesize();
 char*a=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 char*b=mmap(0,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
 check(a!=MAP_FAILED&&b!=MAP_FAILED,"guard mappings");check(!mprotect(a+page,page,PROT_NONE)&&!mprotect(b+page,page,PROT_NONE),"guard protection");
 for(size_t n=0;n<1024;n++){
  unsigned char*x=(unsigned char*)a+page-n-1,*y=(unsigned char*)b+page-n-1;
  for(size_t i=0;i<n;i++)x[i]=y[i]=(unsigned char)(1+(i*37+n)%255);
  x[n]=y[n]=0;
  check(!_platform_strcmp((char*)x,(char*)y),"equal guarded strings");
  check(_platform_strlen((char*)x)==n,"guarded strlen");
  check(_platform_strnlen((char*)x,n/2)==n/2,"bounded strnlen");
  check(!_platform_strncmp((char*)x,(char*)y,n+1),"guarded strncmp");
  check(!_platform_memcmp(x,y,n),"guarded memcmp");
  check(_platform_memchr(x,0,n+1)==x+n,"guarded memchr");
  if(n){size_t at=n/2;y[at]^=1;if(!y[at])y[at]=2;int expected=sign(compare(x,y,n));check(sign(_platform_strcmp((char*)x,(char*)y))==expected,"unsigned differing string");check(sign(_platform_memcmp(x,y,n))==expected,"unsigned differing bytes");}
 }
 unsigned char actual[2048],expected[2048];
 for(unsigned trial=0;trial<10000;trial++){
  for(size_t i=0;i<2048;i++)actual[i]=expected[i]=(unsigned char)(i*17+trial);
  size_t n=(trial*31)%1024,src=(trial*7)%1024,dst=(trial*23)%1024;
  if(dst>src){for(size_t i=n;i;i--)expected[dst+i-1]=expected[src+i-1];}
  else{for(size_t i=0;i<n;i++)expected[dst+i]=expected[src+i];}
  check(_platform_memmove(actual+dst,actual+src,n)==actual+dst,"memmove return");
  check(!compare(actual,expected,2048),"overlap and unaligned memmove");
 }
 char first[192],second[192];for(size_t i=0;i<191;i++)first[i]=second[i]='a'+i%26;first[191]=second[191]=0;
 volatile int result=0;uint64_t start=mach_absolute_time();
 for(unsigned i=0;i<2000000;i++)result+=_platform_strcmp(first,second);
 uint64_t elapsed=mach_absolute_time()-start;mach_timebase_info_data_t scale;mach_timebase_info(&scale);
 printf("PASS: guarded strings, unsigned comparisons, 10000 overlapping copies; strcmp_ns=%.2f (%d)\n",elapsed*(double)scale.numer/scale.denom/2000000,result);
 munmap(a,page*2);munmap(b,page*2);return 0;
}
