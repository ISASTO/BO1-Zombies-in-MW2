#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
namespace mod {
inline bool ContainsBytes(std::uintptr_t base,std::size_t size,std::uintptr_t at,std::size_t length) {
    return length && at>=base && size<=std::numeric_limits<std::uintptr_t>::max()-base
        && length<=size && at-base<=size-length;
}
// Snapshot reads must cover every entity field, not just the struct's first
// read. Values outside the copied arena still use the checked live reader.
template<class Fallback> bool ReadArena(std::uintptr_t base,const std::uint8_t* bytes,std::size_t size,
    std::uintptr_t at,void* out,std::size_t length,Fallback fallback) {
    if(bytes && out && ContainsBytes(base,size,at,length)) {std::memcpy(out,bytes+(at-base),length);return true;}
    return fallback(at,out,length);
}
struct MemoryReadCounts {unsigned long long image=0,kernel=0;};
bool InitializeHostReads(std::uintptr_t base,std::size_t size);
std::size_t HostReadableBytes(std::uintptr_t address);
bool CheckedReadMemory(std::uintptr_t address,void* output,std::size_t size);
MemoryReadCounts GetMemoryReadCounts();
} // namespace mod
