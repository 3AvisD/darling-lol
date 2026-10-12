#include <cstdlib>
#include <cstdio>
#include <new>
#include <objc/runtime.h>
static bool ready;
static unsigned deletes;
void* operator new(std::size_t n) { return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept {
    if (p && !ready) { std::fputs("FAIL application delete used before main\n",stderr); std::abort(); }
    if(p) ++deletes;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
int main() {
    ready=true;
    if(objc_getClassList(nullptr,0)<=0) return 1;
    void* p=::operator new(64); ::operator delete(p);
    if(deletes!=1) return 2;
    std::puts("PASS Objective-C startup uses private allocation; app delete remains active");
    return 0;
}
