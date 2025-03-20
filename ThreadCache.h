#pragma once

#include"Common.h"

class ThreadCache{
public:
    //申请内存对象
    void* Allocate(size_t size);

    //释放内存对象
    void Deallocate(void* ptr, size_t size);

    //从中心缓存获取对象
    void* FetchFromCentralCache(size_t index, size_t size);

    
    void ListTooLong(FreeList& list,size_t size);

private:
    FreeList _freeLists[NFREELISTS];//哈希桶
};

//TLS - Thread Local Storage
#ifdef _WIN32
    static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
#else
    //static __thread ThreadCache* pTLSThreadCache = nullptr;
    static thread_local ThreadCache* pTLSThreadCache = nullptr;
#endif
