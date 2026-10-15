#include "script_heap.hpp"
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
void Check(bool ok,const char* why) {if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Commit {std::size_t* total;bool operator()(std::uintptr_t,std::size_t bytes) const {*total+=bytes;return true;}};
int main() {
    using Heap=mod::ScriptHeap<std::mutex,Commit>;
    constexpr std::size_t reserve=1<<20;
    std::vector<std::uint8_t> storage(reserve+16);
    const auto base=(reinterpret_cast<std::uintptr_t>(storage.data())+15)&~std::uintptr_t(15);
    std::size_t committed=0;
    Heap heap;heap.attach(base,reserve,64<<10,Commit{&committed});
    Check(Heap::sizeClass(1)==0 && Heap::sizeClass(16)==0 && Heap::sizeClass(17)==1 && Heap::sizeClass(0xffff)==12,"native MT size classes");
    // Same class sizes as the native tree: usable bytes = 16 << class.
    auto* a=static_cast<std::uint8_t*>(heap.allocate(100));
    Check(a && !(reinterpret_cast<std::uintptr_t>(a)&15) && heap.owns(a),"16-byte aligned owned block");
    std::memset(a,0xab,128);
    auto* b=static_cast<std::uint8_t*>(heap.allocate(128));
    Check(b && b>=a+128,"class-sized payloads do not overlap");
    Check(heap.stats().used==256,"usage counts class bytes");
    Check(heap.release(a,100) && !heap.release(a,100),"double free rejected");
    Check(heap.allocate(120)==a,"freed block reused within its class");
    Check(!heap.release(a+16,100) && !heap.release(reinterpret_cast<void*>(base+8),16),"interior/misaligned pointers rejected");
    Check(heap.allocate(0x10000)==nullptr && heap.allocate(-1)==nullptr,"unrepresentable sizes refused for native error path");
    heap.reset();
    Check(!heap.release(a,100) && !heap.release(b,128),"stale frees after MT_Init rejected");
    Check(heap.stats().used==0 && heap.stats().rejectedFrees==5,"reset clears usage");
    // Exhaustion returns nullptr (caller falls back to native tree) without overlap.
    std::set<std::uintptr_t> seen;unsigned count=0;
    while(void* p=heap.allocate(4000)){Check(seen.insert(reinterpret_cast<std::uintptr_t>(p)).second,"unique blocks");++count;}
    Check(count==reserve/(4096+16) && committed<=reserve,"reserve bound honored");
    heap.reset();
    // Concurrent alloc/free.
    std::vector<std::thread> threads;
    for(int t=0;t<4;++t)threads.emplace_back([&heap,t]{
        std::vector<std::pair<void*,int>> live;
        for(int n=0;n<20000;++n){
            const int size=16+((n*37+t*11)%2000);
            if(void* p=heap.allocate(size)){std::memset(p,t,size);live.push_back({p,size});}
            if(live.size()>64){auto [p,s]=live.front();live.erase(live.begin());
                for(int i=0;i<s;++i)if(static_cast<std::uint8_t*>(p)[i]!=t){std::cerr<<"FAIL: block shared between threads\n";std::exit(1);}
                heap.release(p,s);}
        }
        for(auto [p,s]:live)heap.release(p,s);
    });
    for(auto& t:threads)t.join();
    Check(heap.stats().used==0,"concurrent frees balance");
    std::cout<<"script heap ok\n";
}
