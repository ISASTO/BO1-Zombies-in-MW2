#pragma once
// Owned heap for the native script memory tree's pointer allocations.
// Power-of-two classes of 16-byte nodes exactly like the native tree, so
// callers that compare size classes (MT_Realloc 0x217830) keep their meaning.
// Each block carries a 16-byte header before its payload, so a free that the
// native tree would silently absorb (stale after MT_Init, double free, wrong
// pointer) can never put a live block back on a free list.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace mod {
template<class Lock,class Commit> class ScriptHeap {
public:
    static constexpr unsigned kClasses=13;                       // 16 B .. 64 KiB
    struct Header {std::uint32_t tag,generation;std::uint16_t cls,state;std::uint32_t reserved;};
    static_assert(sizeof(Header)==16,"header keeps 16-byte payload alignment");
    static constexpr std::uint32_t kTag=0x4d544850;
    static constexpr std::uint16_t kLive=0x4c49,kFree=0x4652;
    struct Stats {std::uint64_t used=0,peak=0;std::size_t committed=0,reserved=0;unsigned allocations=0,rejectedFrees=0,sizeMismatches=0,generation=0;};

    void attach(std::uintptr_t base,std::size_t reserve,std::size_t commitStep,Commit commit) {
        base_=base;reserve_=reserve;step_=commitStep;commit_=commit;next_=0;committed_=0;
    }
    std::uintptr_t base() const {return base_;}
    std::size_t reserve() const {return reserve_;}
    static int sizeClass(int size) {
        if(size<=0)size=1;
        unsigned nodes=(static_cast<unsigned>(size)+15u)/16u,cls=0;
        while((1u<<cls)<nodes)++cls;
        return static_cast<int>(cls);
    }
    // nullptr: size not representable or reserve exhausted (caller falls back).
    void* allocate(int size) {
        if(size<0 || size>=0x10000 || !base_)return nullptr;
        const int cls=sizeClass(size);const std::size_t bytes=(std::size_t(16)<<cls)+sizeof(Header);
        std::uint8_t* block=nullptr;
        lock_.lock();
        if(free_[cls]) {
            block=free_[cls];std::memcpy(&free_[cls],block+sizeof(Header),sizeof(void*));
        } else if(next_+bytes<=reserve_) {
            bool ok=true;
            while(next_+bytes>committed_) {
                const std::size_t grow=committed_+step_>reserve_?reserve_-committed_:step_;
                if(!grow || !commit_(base_+committed_,grow)){ok=false;break;}
                committed_+=grow;
            }
            if(ok){block=reinterpret_cast<std::uint8_t*>(base_+next_);next_+=bytes;}
        }
        if(block) {
            const Header header{kTag,generation_,static_cast<std::uint16_t>(cls),kLive,0};
            std::memcpy(block,&header,sizeof(header));
        }
        lock_.unlock();
        if(!block)return nullptr;
        allocations_.fetch_add(1);account(static_cast<std::int64_t>(std::size_t(16)<<cls));
        return block+sizeof(Header);
    }
    bool owns(const void* pointer) const {
        return reinterpret_cast<std::uintptr_t>(pointer)-base_<reserve_;
    }
    // false: rejected (not a live block of the current generation).
    bool release(void* pointer,int size) {
        auto* payload=static_cast<std::uint8_t*>(pointer);
        const auto at=reinterpret_cast<std::uintptr_t>(payload);
        bool accepted=false;int cls=0;
        lock_.lock();
        if(!(at&15) && at>=base_+sizeof(Header) && at-base_<=next_) {
            Header header;std::memcpy(&header,payload-sizeof(header),sizeof(header));
            if(header.tag==kTag && header.generation==generation_ && header.state==kLive && header.cls<kClasses
               && at-base_+(std::size_t(16)<<header.cls)<=next_) {
                cls=header.cls;header.state=kFree;std::memcpy(payload-sizeof(header),&header,sizeof(header));
                std::memcpy(payload,&free_[cls],sizeof(void*));free_[cls]=payload-sizeof(header);accepted=true;
            }
        }
        lock_.unlock();
        if(!accepted){rejected_.fetch_add(1);return false;}
        if(sizeClass(size)!=cls)mismatches_.fetch_add(1);
        account(-static_cast<std::int64_t>(std::size_t(16)<<cls));
        return true;
    }
    // MT_Init: every block of the previous generation becomes invalid.
    void reset() {
        lock_.lock();next_=0;free_={};++generation_;lock_.unlock();
        used_.store(0);
    }
    Stats stats() {
        Stats s;s.used=used_.load();s.peak=peak_.load();s.reserved=reserve_;
        lock_.lock();s.committed=committed_;s.generation=generation_;lock_.unlock();
        s.allocations=allocations_.load();s.rejectedFrees=rejected_.load();s.sizeMismatches=mismatches_.load();
        return s;
    }
private:
    void account(std::int64_t delta) {
        const auto value=used_.fetch_add(static_cast<std::uint64_t>(delta))+static_cast<std::uint64_t>(delta);
        auto peak=peak_.load();
        while(value>peak && !peak_.compare_exchange_weak(peak,value)){}
    }
    Lock lock_{};
    Commit commit_{};
    std::uintptr_t base_=0;
    std::size_t reserve_=0,step_=0,next_=0,committed_=0;
    std::uint32_t generation_=1;
    std::array<std::uint8_t*,kClasses> free_{};
    std::atomic<std::uint64_t> used_{0},peak_{0};
    std::atomic<unsigned> allocations_{0},rejected_{0},mismatches_{0};
};
} // namespace mod
