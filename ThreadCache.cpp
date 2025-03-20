#include"ThreadCache.h"
#include "CentralCache.h"


//申请内存对象
void* ThreadCache::Allocate(size_t size){
    //thread cache只处理小于等于MAX_BYTES的内存申请
    assert(size<=MAX_BYTES);
    size_t alignSize = SizeClass::RoundUp(size);
    size_t index = SizeClass::Index(size);


    if (!_freeLists[index].Empty()) {
		return _freeLists[index].Pop();
	}
	else {
		return FetchFromCentralCache(index, alignSize);
	}
}

//释放内存对象
void ThreadCache::Deallocate(void* ptr,size_t size){
    assert(ptr);
    assert(size<=MAX_BYTES);
    
    //找出对应的自由链表桶将对象插入
    size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

    //当自由链表长度大于一次批量申请的对象个数时就开始还一段list给central cache
    if(_freeLists[index].Size()>=_freeLists[index].MaxSize()){
        
        ListTooLong(_freeLists[index],size);
    }

}


void* ThreadCache::FetchFromCentralCache(size_t index, size_t size){
    //慢开始反馈调节算法
    //1.最开始不会一次向central cache一次批量要太多，因为要太多了可能会用不完
    //2.如果你不断有size大小的内存需求，那么batchNum就会不断的增长，直到上限
    
    /*
    在C++的algorithm头文件中有一个min函数，这是一个函数模板，而在Windows.h头文件中也有一个min，这是一个宏。
    由于调用函数模板时需要进行参数类型的推演，因此当我们调用min函数时，编译器会优先匹配Windows.h当中以宏的形式实现的min，
    此时当我们以std::min的形式调用min函数时就会产生报错，这就是没有用命名空间进行封装的坏处，
    这时我们只能选择将std::去掉，让编译器调用Windows.h当中的min。
    */
    
    #ifdef _WIN32
    size_t batchNum=min(_freeLists[index].MaxSize(),SizeClass::NumMoveSize(size));
    #else
    size_t batchNum=std::min(_freeLists[index].MaxSize(),SizeClass::NumMoveSize(size));
    #endif
    
    //cout<<"batchNum:"<<batchNum<<endl;
    //exit(1);

    if(batchNum==_freeLists[index].MaxSize()){
        _freeLists[index].MaxSize()+=1;
    }

    void* start = nullptr;
    void* end=nullptr;
    size_t actualNum=CentralCache::GetInstance()->FetchRangeObj(start,end,batchNum,size);
    assert(actualNum>=1); // 至少有一个

    //申请到的内存对象的个数是一个，则直接将这一个对象返回即可
    if(actualNum==1){
        assert(start==end);
        return start;
    }
    else{
        //申请到的内存对象的个数是多个，还需要将剩下的对象挂到thread cache中对应的哈希桶中。
        _freeLists[index].PushRange(NextObj(start),end,actualNum-1);
        return start;
    }
}

void ThreadCache::ListTooLong(FreeList& list,size_t size){
    void* start=nullptr;
    void* end=nullptr;
    list.PopRange(start,end,list.MaxSize());
    
    
    //将取出的对象还给central cache中对应的span
    CentralCache::GetInstance()->ReleaseListToSpans(start,size);
}
