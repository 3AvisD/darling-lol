#include <darlingserver/message.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>

using DarlingServer::Message;
static bool same(const ucred& a,const ucred& b) {
 return a.pid==b.pid&&a.uid==b.uid&&a.gid==b.gid;
}
static double benchmark(unsigned count) {
 auto start=std::chrono::steady_clock::now();
 for(unsigned i=0;i<count;++i) {Message message(1,0);}
 return std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/count;
}
int main() {
 const ucred expected={getpid(),getuid(),getgid()};
 double live=benchmark(100000);
 Message::initializeServerCredentials();
 double cached=benchmark(100000);
 std::atomic<bool> valid=true;
 std::vector<std::thread> workers;
 for(unsigned n=0;n<4;++n) workers.emplace_back([&]{
  for(unsigned i=0;i<10000;++i) {
   Message message(1,0);ucred observed;
   if(!message.copyCredentialsOut(observed)||!same(observed,expected))valid=false;
  }
 });
 for(auto& worker:workers)worker.join();
 Message custom(1,0);ucred changed={expected.pid+1,expected.uid+1,expected.gid+1},observed;
 custom.copyCredentialsIn(changed);
 bool overrides=custom.copyCredentialsOut(observed)&&same(observed,changed);
 custom.setPID(expected.pid);custom.setUID(expected.uid);custom.setGID(expected.gid);
 overrides=overrides&&custom.copyCredentialsOut(observed)&&same(observed,expected);
 // Fork after initialization: the child must never reuse its parent's PID.
 pid_t child=fork();
 if(child==0) {
  Message message(1,0);ucred credentials;
  const ucred current={getpid(),getuid(),getgid()};
  _exit(message.copyCredentialsOut(credentials)&&same(credentials,current)?0:1);
 }
 int status=0;bool forkPass=child>0&&waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
 int sockets[2];
 if(socketpair(AF_UNIX,SOCK_SEQPACKET,0,sockets)!=0)return 2;
 int enabled=1;
 if(setsockopt(sockets[1],SOL_SOCKET,SO_PASSCRED,&enabled,sizeof(enabled))!=0)return 3;
 unsigned transferred=0;
 for(unsigned i=0;i<200;++i) {
  int pipeFDs[2];if(pipe(pipeFDs))return 4;
  char pattern=42;if(write(pipeFDs[1],&pattern,1)!=1)return 5;close(pipeFDs[1]);
  Message outgoing(1,0);outgoing.pushDescriptor(pipeFDs[0]);outgoing.data()[0]=7;
  outgoing.rawHeader().msg_name=nullptr;outgoing.rawHeader().msg_namelen=0;
  if(sendmsg(sockets[0],&outgoing.rawHeader(),0)!=1)return 6;
  Message incoming(1,1);
  if(recvmsg(sockets[1],&incoming.rawHeader(),0)!=1||incoming.rawHeader().msg_flags&MSG_CTRUNC)return 7;
  if(!incoming.copyCredentialsOut(observed)||!same(observed,expected))return 8;
  int received=incoming.extractDescriptorAtIndex(0);char value=0;
  if(received<0||read(received,&value,1)!=1||value!=pattern||incoming.data()[0]!=7)return 9;
  close(received);++transferred;
 }
 close(sockets[0]);close(sockets[1]);
 bool pass=valid&&overrides&&forkPass&&transferred==200;
 printf("HOST_MESSAGE pass=%d concurrent=40000 overrides=%d fork_identity=%d transferred=%u live_ns=%.1f cached_ns=%.1f\n",pass,overrides,forkPass,transferred,live,cached);
 return pass?0:1;
}
