#include"CentralCache.h"
#include "PageCache.h"

CentralCache CentralCache::_sInst;
ObjectPool<Span> SpanList::_spanPool;

//从central cache获取一定数量的对象给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t size) {
	
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); //加锁
	//exit(0);
	//在对应哈希桶中获取一个非空的span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span); //span不为空
	assert(span->_freeList); //span当中的自由链表也不为空

	//从span中获取n个对象
	//如果不够n个，有多少拿多少
	start = span->_freeList;
	end = span->_freeList;
	size_t actualNum = 1;
	while (NextObj(end)&&n - 1) {
		end = NextObj(end);
		actualNum++;
		n--;
	}
	span->_freeList = NextObj(end); //取完后剩下的对象继续放到自由链表
	NextObj(end) = nullptr; //取出的一段链表的表尾置空
	span->_useCount += actualNum; //更新被分配给thread cache的计数
	//cout<<"span->_useCount="<<span->_useCount<<endl;
	//exit(0);

	_spanLists[index]._mtx.unlock(); //解锁
	return actualNum;
}

Span* CentralCache::GetOneSpan(SpanList& spanList, size_t size){
	//1.先在spanList中寻找非空的span
	Span* it=spanList.Begin();
	while(it !=spanList.End()){
		if(it->_freeList!=nullptr){
			return it;
		}
		else{
			it=it->_next;
		}
	}
	//2.spanList中没有非空的span，只能向page cache申请
	//先把central cache的桶锁解掉，这样如果其他对象释放内存对象回来，不会被阻塞
	spanList._mtx.unlock();
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span=PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_objSize=size;
	
	span->_isUse=true;
	//该span会被切成一个个size大小的对象。

	PageCache::GetInstance()->_pageMtx.unlock();
	//获取到span后不需要立即重新加上central cache的桶锁。

	//获取到的span后不需要立即重新加上central cache的桶锁，因为还需要进行切分

	//计算span的大块内存的起始地址和大块内存的大小（字节数）
	char* start=(char*) (span->_pageId<<PAGE_SHIFT);
	size_t bytes=span->_n<<PAGE_SHIFT;

	//把大块内存切成size大小的对象链接起来
	char* end=start+bytes;
	//先切一块去做尾，方便尾插,保证内存的顺序
	span->_freeList=start;
	start+=size;
	void* tail=span->_freeList;
	//尾插
	while(start<end){
		NextObj(tail)=start;
		tail=NextObj(tail);
		start+=size;
	}
	NextObj(tail)=nullptr;//尾的指向置空
	
	//将切好的span头插到spanList中
	//span切分完毕后，需要挂到桶里面的时候再重新加桶锁
	spanList._mtx.lock();
	spanList.PushFront(span);

	return span;
}

void CentralCache::ReleaseListToSpans(void* start, size_t size){
	size_t index=SizeClass::Index(size);

	_spanLists[index]._mtx.lock(); //加锁,桶锁
	while(start){
		void* next=NextObj(start); // 记录下一个
		Span* span=PageCache::GetInstance()->MapObjectToSpan(start);
		//将对象头插到span的自由链表
		NextObj(start)=span->_freeList;
		span->_freeList=start;

		span->_useCount--; //更新被分配给thread cache的计数
		
		if(span->_useCount==0){
			//说明这个span分配出去的对象全部回来了
			//此时这个span就可以再回收给page cache，page cache可以再尝试去做前后页的合并
			//我们这里的逻辑是，只要span中的对象全部回来，就直接回收给page cache
			//cout<<"span->_useCount="<<span->_useCount<<endl;
			//exit(0);

			_spanLists[index].Erase(span);
			span->_freeList=nullptr;
			span->_prev=nullptr;
			span->_prev=nullptr;

			//释放span给page cache时，使用page cache的锁就可以了，这时把通锁解掉
			_spanLists[index]._mtx.unlock();//解桶锁
			
			PageCache::GetInstance()->_pageMtx.lock(); //加大锁
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();//解大锁
			
			_spanLists[index]._mtx.lock(); //加桶锁
		}
		start=next;
	}
	_spanLists[index]._mtx.unlock();//解锁
}