#pragma once

#include"Common.h"
#include"ThreadCache.h"
#include"PageCache.h"
#include"ObjectPool.h"


static void* ConcurrentAlloc(size_t size){

    if(size>MAX_BYTES){
        //计算出对齐后需要申请的页数
        size_t alignSize = SizeClass::RoundUp(size);
        size_t kPage=alignSize>>PAGE_SHIFT;

        //向page cache申请kPage页的span
        PageCache::GetInstance()->_pageMtx.lock();
        Span* span=PageCache::GetInstance()->NewSpan(kPage);
        //span->_objSize=size;
        span->_objSize=alignSize;
        PageCache::GetInstance()->_pageMtx.unlock();

        void* ptr=(void*)(span->_pageId<<PAGE_SHIFT);
        return ptr;
    }
    else{
        if(pTLSThreadCache == nullptr){
            static std::mutex tcMtx;
            static ObjectPool<ThreadCache> tcPool;
            tcMtx.lock();
            //pTLSThreadCache = new ThreadCache;
            pTLSThreadCache=tcPool.New();
            tcMtx.unlock();
        }
        //std::cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
        return pTLSThreadCache->Allocate(size);
    }
}


// 静态函数，用于释放并发缓存中的内存
static void ConcurrentFree(void* ptr){

    Span* span=PageCache::GetInstance()->MapObjectToSpan(ptr);
    size_t size=span->_objSize;

    if(size>MAX_BYTES){
        PageCache::GetInstance()->_pageMtx.lock();
        PageCache::GetInstance()->ReleaseSpanToPageCache(span);
        PageCache::GetInstance()->_pageMtx.unlock();
    }
    else{
        assert(pTLSThreadCache);
	    pTLSThreadCache->Deallocate(ptr, size);
    }
}

