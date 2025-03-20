#pragma once

#include <iostream>
#include <vector>
#include <ctime>
#include <cassert>
#include <thread>
#include <mutex>
#include <algorithm>
#include <unordered_map>
#include <atomic>
#include "ObjectPool.h"

using std::cout;
using std::endl;


#ifdef _WIN32
#include<Windows.h>
#else
//linux下
#include <sys/mman.h>
#include <unistd.h>
#endif



//小于等于MAX_BYTES，就找thread cache申请
//大于MAX_BYTES，就直接找page cache或者系统堆申请
static const size_t MAX_BYTES = 256 * 1024;
//thread cache和central cache自由链表哈希桶的表大小
static const size_t NFREELISTS = 208;
//page cache中哈希桶的个数
static const size_t NPAGES = 129;
//页大小转移偏移,即一页定义为2^13,也就是8KB
static const size_t PAGE_SHIFT = 13;


#ifdef _WIN64
typedef unsigned long long PAGE_ID;
#elif _WIN32
typedef size_t PAGE_ID;
#elif defined(__x86_64__) || defined(__ppc64__)
typedef unsigned long long PAGE_ID; // Linux 64 位系统
#else
typedef size_t PAGE_ID; // Linux 32 位系统
#endif


//直接去堆上申请按页申请空间
inline static void* SystemAlloc(size_t kpage) {
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	//VirtualAlloc 是 Windows API 中的一个函数，用于在虚拟地址空间中分配内存。
	//第一个参数 0 表示让系统自动选择分配内存的起始地址
	//kpage << 13 是将 kpage 左移 13 位，相当于乘以 2^13 = 8192，因为通常一页的大小是 8KB
	//MEM_COMMIT | MEM_RESERVE 表示既保留虚拟地址空间又提交物理内存
	//PAGE_READWRITE 表示分配的内存区域具有读写权限
#else
	void* ptr = mmap(0, kpage << PAGE_SHIFT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	//请求系统分配 kpage 页大小（每页 8KB）的匿名内存，
	// 该内存区域既可以读也可以写，并且是私有的，不与任何文件关联。
	// 如果映射成功，mmap 函数会返回映射区域的起始地址；
	// 如果失败，会返回 MAP_FAILED
	if (ptr == MAP_FAILED) {
		ptr = nullptr;
	}
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

//直接将内存还给堆
inline static void SystemFree(void* ptr) {
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	munmap(ptr, 0);
#endif
}


// static void*& NextObj(void* ptr) {
//     return (*(void**)ptr);
// }

//管理切分好的小对象的自由链表
class FreeList {
public:
	//将释放的对象头插到自由链表
	void Push(void* obj) {
		assert(obj);
		//头插
		NextObj(obj) = _freeList;
		_freeList = obj;
        _size++;
	}
	//从自由链表头部获取一个对象
	void* Pop() {
		assert(_freeList);
		//头删
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
        _size--;

		return obj;
	}
    //插入一段范围的对象到自由链表
    void PushRange(void* start,void* end,size_t n){
        assert(start);
        assert(end);

        //头插
        NextObj(end)=_freeList;
        _freeList=start;
        _size+=n;
        
    }
    //从自由链表获取一段范围的对象
    void PopRange(void*& start, void*& end, size_t n){
        assert(n <= _size);
		//头删
		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1;i++) {
			end = NextObj(end);
		}
		_freeList = NextObj(end); //自由链表指向end的下一个对象
		NextObj(end) = nullptr; //取出的一段链表的表尾置空
		_size -= n;
    }

    //从自由链表
    bool Empty() {
		return _freeList == nullptr;
	}

    size_t& MaxSize() {
		return _maxSize;
	}

    size_t Size() {
		return _size;
	}

private:
	void* _freeList = nullptr;//自由链表
    size_t _maxSize=1;
    size_t _size=0;
};


//管理对齐和映射等关系
class SizeClass {
public:
    //位运算写法
	static inline size_t _RoundUp(size_t bytes, size_t alignNum) {
		return ((bytes + alignNum - 1)&~(alignNum - 1));
	}

    //获取向上对齐后的字节数
    static inline size_t RoundUp(size_t bytes) {
        if (bytes <= 128) {
            return _RoundUp(bytes, 8);
        }
        else if (bytes <= 1024) {
            return _RoundUp(bytes, 16);
        }
        else if (bytes <= 8 * 1024) {
            return _RoundUp(bytes, 128);
        }
        else if (bytes <= 64 * 1024) {
            return _RoundUp(bytes, 1024);
        }
        else if (bytes <= 256 * 1024) {
            return _RoundUp(bytes, 8 * 1024);
        }
        else {
            //assert(false);
            //return -1;
			//大于256KB的按页对齐
			return _RoundUp(bytes,1<<PAGE_SHIFT);
        }
    }

    //位运算写法
	static inline size_t _Index(size_t bytes, size_t alignShift) {
		return ((bytes + (1 << alignShift) - 1) >> alignShift) - 1;
	}

    //获取对应哈希桶的下标
    static inline size_t Index(size_t bytes) {
		//每个区间有多少个自由链表
		static size_t groupArray[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + groupArray[0];
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + groupArray[0] + groupArray[1];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + groupArray[0] + groupArray[1] + groupArray[2];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + groupArray[0] + groupArray[1] + groupArray[2] + groupArray[3];
		}
		else {
			assert(false);
			return -1;
		}
	}

    //thread cache一次从central cache获取对象的上限
    static size_t NumMoveSize(size_t size){
        assert(size>0);

        //对象越小，计算出的上限越高
        //对象越大，计算出的上限越低

        int num=MAX_BYTES/size;
        if(num<2)
            num=2;
        
        if(num>512)
            num=512;

        return num;
    }

    //central cache一次向page cache获取多少页
	static size_t NumMovePage(size_t size) {
		size_t num = NumMoveSize(size); //计算出thread cache一次向central cache申请对象的个数上限
		size_t nPage = num*size; //num个size大小的对象所需的字节数
		

		nPage >>= PAGE_SHIFT; //将字节数转换为页数
		if (nPage == 0) //至少给一页
			nPage = 1;

		return nPage;
	}

};

//管理以页为单位的大块内存
struct Span{
    PAGE_ID _pageId=0; //大块内存起始页的页号
    size_t _n=0;       //页的数量

    Span* _next=nullptr; //双链表结构
    Span* _prev=nullptr;

    size_t _objSize=0; //切好的小对象的大小
    size_t _useCount=0; //切好的小块内存，被分配给thread cache的计数

    void* _freeList=nullptr; //切好的小块内存的自由链表

    bool _isUse=false; //是否被使用
};

//带头双向循环链表
class SpanList{
public:
    SpanList(){
        //_head=new Span;
        _head = _spanPool.New();
        _head->_next=_head;
        _head->_prev=_head;
    }
    Span* Begin() {
		return _head->_next;
	}
	Span* End() {
		return _head;
	}
	bool Empty() {
		return _head == _head->_next;
	}
	void PushFront(Span* span) {
		Insert(Begin(), span);
	}
	Span* PopFront() {
		Span* front = _head->_next;
		Erase(front);
		return front;
	}
	void Insert(Span* pos, Span* newSpan) {
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;

		prev->_next = newSpan;
		newSpan->_prev = prev;

		newSpan->_next = pos;
		pos->_prev = newSpan;
	}
	void Erase(Span* pos) {
		assert(pos);
		assert(pos != _head); //不能删除哨兵位的头结点

		//if (pos == _head) {
		//	int x = 0;
		//}

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
	}

private:
    Span* _head;
    static ObjectPool<Span> _spanPool;

public:
    std::mutex _mtx; // 桶锁
};