#include"PageCache.h"

PageCache PageCache::_sInst;

//获取一个k页的span
Span* PageCache::NewSpan(size_t k) {
    assert(k > 0);
    if (k > NPAGES - 1) {
        // cout<<"k: "<<k<<endl;
        // exit(0);
        void* ptr = SystemAlloc(k);
        Span* span = _spanPool.New();

        //Span* span = new Span;
        span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
        span->_n = k;

        //建立页号与span之间的映射
        //_idSpanMap[span->_pageId] = span;
        _idSpanMap.set(span->_pageId, span);
        return span;
    }
    // cout<<"k: "<<k<<endl;
    // exit(0);
    //先检查第k个桶里面有没有span
    if (!_spanLists[k].Empty()) {
        Span* kSpan = _spanLists[k].PopFront();
        //建立页号与span的映射，方便central cache回收小块内存时查找对应的span
        for (PAGE_ID i = 0; i < kSpan->_n; i++) {
            //_idSpanMap[kSpan->_pageId + i] = kSpan;
            _idSpanMap.set(kSpan->_pageId + i, kSpan);
        }
        return kSpan;
    }
    //检查一下后面的桶里面有没有span，如果有可以将其进行切分
    for (size_t i = k + 1; i < NPAGES; i++) {
        if (!_spanLists[i].Empty()) {

            Span* nSpan = _spanLists[i].PopFront();
            //Span* kSpan=new Span;
            Span* kSpan = _spanPool.New();
            //在nSpan的头部切k页下来
            kSpan->_pageId = nSpan->_pageId;
            kSpan->_n = k;
            //cout<<"kSpan->_pageId: "<<kSpan->_pageId<<endl;
            //cout<<"kSpan->_n: "<<kSpan->_n<<endl;

            nSpan->_pageId += k;
            nSpan->_n -= k;
            // cout<<"nSpan->_pageId: "<<nSpan->_pageId<<endl;
            // cout<<"nSpan->_n: "<<nSpan->_n<<endl;
            // // exit(0);

            //将剩下的挂到对应的映射的位置
            _spanLists[nSpan->_n].PushFront(nSpan);

            //存储nSpan的首位页号与nSpan之间的映射，方便page cache合并span时进行前后页的查找
            //_idSpanMap[nSpan->_pageId] = nSpan;
            //_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
              
            _idSpanMap.set(nSpan->_pageId,nSpan);
            _idSpanMap.set(nSpan->_pageId+nSpan->_n-1,nSpan);

            //建立页号与span的映射，方便central cache回收小块内存时查找对应的span
            for (PAGE_ID i = 0; i < kSpan->_n; i++) {
                //_idSpanMap[kSpan->_pageId + i] = kSpan;
                _idSpanMap.set(kSpan->_pageId + i, kSpan);
            }

            return kSpan;
        }
    }
    //如果走遇到这里说明后面没有大页的span了，这时就向堆申请一个128页的span
    //Span* bigSpan=new Span;
    Span* bigSpan = _spanPool.New();
    void* ptr = SystemAlloc(NPAGES - 1);
    bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
    bigSpan->_n = NPAGES - 1;


    _spanLists[bigSpan->_n].PushFront(bigSpan);
    //尽量避免代码重复，递归调用自己
    return NewSpan(k);
}

Span* PageCache::MapObjectToSpan(void* obj) {
    PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; //得到页号

    //std::unique_lock<std::mutex> lock(_pageMtx); //构造时加锁，析构时自动解锁
    //auto ret = _idSpanMap.find(id);
    //if (ret != _idSpanMap.end()) {
    //    return ret->second;
    //}
    //else {
    //    assert(false);
    //    return nullptr;
    //}
    Span* ret = (Span*)_idSpanMap.get(id);
    assert(ret != nullptr);
    return ret;
}


//释放空闲的span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span) {
    //对span的前后页，尝试进行合并，缓解内存碎片的问题
    //1.向前合并

    if (span->_n > NPAGES - 1) {
        //大于128页直接释放给堆
        void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
        SystemFree(ptr);
        //delete span;
        _spanPool.Delete(span);
        return;
    }
    //对span的前后页，尝试进行合并缓解内存碎片问题
    //1.向前合并
    while (1) {
        PAGE_ID prevId = span->_pageId - 1;
        //auto ret = _idSpanMap.find(prevId);
        ////前面的页号没有(还没有向系统申请)，则停止向前合并
        //if (ret == _idSpanMap.end()) {
        //    break;
        //}
        Span* ret = (Span*)_idSpanMap.get(prevId);
        if (ret == nullptr) {
            break;
        }
        //前面的页号对应的span正在被使用，停止向前合并
        //Span* prevSpan = ret->second;
        Span* prevSpan = ret;
        if (prevSpan->_isUse == true) {
            break;
        }
        //合并超过1128页的span无法进行管理，停止向前合并
        if (prevSpan->_n + span->_n > NPAGES - 1) {
            break;
        }
        //进行向前合并
        span->_pageId = prevSpan->_pageId;
        span->_n += prevSpan->_n;

        //将preSpan从对应的双链表中移除
        _spanLists[prevSpan->_n].Erase(prevSpan);

        //delete prevSpan;
        _spanPool.Delete(prevSpan);
    }
    //2.向后合并
    while (1) {
        PAGE_ID nextId = span->_pageId + span->_n;
        //auto ret = _idSpanMap.find(nextId);
        ////后面的页号还没有(还未向系统申请),停止向后合并
        //if (ret == _idSpanMap.end()) {
        //    break;
        //}
        //后面的页号对应的span正在被使用，停止向后合并
        Span* ret = (Span*)_idSpanMap.get(nextId);
        if (ret == nullptr) {
            break;
        }
        //Span* nextSpan = ret->second;
        Span* nextSpan = ret;
        if (nextSpan->_isUse == true) {
            break;
        }
        //合并超过128页的span无法进行管理，停止向后合并
        if (nextSpan->_n + span->_n > NPAGES - 1) {
            break;
        }
        //进行向后合并
        span->_n += nextSpan->_n;
        //cout<<"span->_n: "<<span->_n<<endl;
        //cout<<"span->_pageId: "<<span->_pageId<<endl;
        //exit(0);

        //将nextSpan从对应的双链表中移除
        _spanLists[nextSpan->_n].Erase(nextSpan);

        _spanPool.Delete(nextSpan);
        //delete nextSpan;
    }
    //将合并后的span挂到对应的双链表当中
    _spanLists[span->_n].PushFront(span);
    ////建立该span与其尾页的映射
    //_idSpanMap[span->_pageId] = span;
    //_idSpanMap[span->_pageId + span->_n - 1] = span;

    _idSpanMap.set(span->_pageId, span);
    _idSpanMap.set(span->_pageId + span->_n - 1, span);

    //将该span设置为未使用的状态
    span->_isUse = false;
}