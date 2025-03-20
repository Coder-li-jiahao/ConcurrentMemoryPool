## 项目介绍

​	本项目实现的是一个高并发的内存池，它的原型是Google的一个开源项目tcmalloc，tcmalloc全程Thread-Caching Malloc，即线程缓存的malloc，实现了高效的多线程内存管理，用于替换系统的内存分配相关函数malloc和free。
![image-20250315145908947](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151500506.png)

​	tcmalloc的知名度也是非常高的，不少公司都在用它，比如Go语言就直接用它做了自己的内存分配器。该项目就是把tcmalloc中最核心的框架简化后拿出来，模拟实现一个mini版的高并发内存池，目的就是学习tcmalloc的精华。

​	该项目主要涉及C/C++、数据结构（链表、哈希桶）、操作系统内存管理、单例模式、多线程、互斥锁等方面的技术。

## 内存池介绍

>池化技术

​	在说内存池之前，我们得先了解一下“池化技术”。所谓“池化技术”，就是程序先向系统申请过量的资源，然后自己进行管理，以备不时之需。

​	之所以要申请过量的资源，是因为申请和释放资源都有较大的开销，不如提前申请一些资源放入“池”中，当需要资源时直接从“池”中获取，不需要时就将该资源重新放回“池”中即可。这样使用时就会变得非常快捷，可以大大提高程序的运行效率。

​	在计算机中，有很多使用“池”这种技术的地方，除了内存池之外，还有连接池、线程池、对象池等。以服务器上的线程池为例，它的主要思想就是：先启动若干数量的线程，让它们处于睡眠状态，当接收到客户端的请求时，唤醒池中某个睡眠的线程，让它来处理客户端的请求，当处理完这个请求后，线程又进入睡眠状态。

>内存池

​	内存池是指程序预先向操作系统申请一块足够大的内存，此后，当程序中需要申请内存的时候，不是直接向操作系统申请，而是直接从内存池中获取；同理，当释放内存的时候，并不是真正将内存返回给操作系统，而是将内存返回给内存池。当程序退出时（或某个特定时间），内存池才将之前申请的内存真正释放。

>内存池主要解决的问题

​	内存池主要解决的就是效率问题，它能够避免让程序频繁的向系统申请和释放内存，其次，内存池作为系统的内存分配器，还需要尝试解决内存碎片的问题。

​	内存碎片分为内部碎片和外部碎片【OS概念】：

- 外部碎片是一些空闲的小块内存区域，由于这些内存空间不连续，以至于合计的内存足够，但是不能满足一些内存分配申请的需求。
- 内部碎片是由于一些对齐的需求，导致分配出去的空间中一些内存无法被利用。

![image-20250315152300690](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151523794.png)

**注意**：内存池尝试解决的是外部碎片的问题，同时也尽可能的减少内部碎片的产生。

>malloc

​	C/C++中我们要动态申请内存并不是直接去堆申请的，而是通过malloc函数去申请的，包括C++中的new世界上也是封装了malloc函数的。

​	我们申请内存块时是先调用malloc，malloc再去向操作系统申请内存。malloc实际上就是一个内存池，malloc相当于向操作系统“批发”了一块较大的内存空间，然后“零售”给程序用，当全部“收完”或程序有大量的内存需求时，再根据实际需求向操作系统“进货”。

![image-20250315152858936](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151528029.png)

​	malloc的实现方式有很多种，一般不同编译器平台用的都是不同的。比如Windows的VS系列中的malloc就是微软自行实现的，而Linux下的gcc用的是glibc中的ptmalloc

## 定长内存池的实现

​	malloc其实就是一个通用的内存池，在什么场景下都可以使用，但这也意味着malloc在什么场景下都不会有很高的性能，因为malloc并不是针对某种场景专门设计的。

​	定长内存池就是针对固定大小内存块的申请和释放的内存池，由于定长内存池只需要支持固定大小内存块的申请和释放，因此我们可以将其性能做到极致，并且在实现定长内存池时不需要考虑内存碎片等问题，因为我们申请/释放都是固定大小的内存块。

​	我们可以通过实现定长内存池来熟悉一下对简单内存池的控制，其次，这个定长内存池后面会作为高并发内存池的一个基础组件。

>如何实现定长

​	在实现定长内存池时要做到“定长”有很多种方法，比如我们可以使用非类型模板参数，使得在该内存池中申请到的对象大小都是N。

```c++
template<size_t N>
class ObjectPool
{};
```

​	此外，定长内存池也叫对象池，在创建对象池时，对象池可以根据传入的对象类型的大小来实现“定长”，因此我们可以通过使用模板参数来实现“定长”，比如创建定长内存池时传入的对象类型是int，那么该内存池就只支持4字节大小内存的申请和释放。

```c++
template<class T>
class ObjectPool
{};
```

>如何直接向堆申请空间？

​	既然是内存池，那么我们得先向系统申请一块内存空间，然后对齐进行管理。想要直接向堆申请内存空间，在Windows下，可以调用VirtualAlloc函数；在Linux下，可以调用brk或者mmap函数。

```c++
#include <iostream>
#include <stdexcept>
#include <cstddef>

#ifdef _WIN32
	#include<Windows.h>
#else
	//linux下
	#include <sys/mman.h>
	#include <unistd.h>
#endif

//直接去堆上申请按页申请空间
inline static void* SystemAlloc(size_t kpage){
#ifdef _WIN32
	void* ptr=VirtualAlloc(0,kpage<<13,MEM_COMMIT | MEM_RESERVE,PAGE_READWRITE);
    //VirtualAlloc 是 Windows API 中的一个函数，用于在虚拟地址空间中分配内存。
    //第一个参数 0 表示让系统自动选择分配内存的起始地址
    //kpage << 13 是将 kpage 左移 13 位，相当于乘以 2^13 = 8192，因为通常一页的大小是 8KB
    //MEM_COMMIT | MEM_RESERVE 表示既保留虚拟地址空间又提交物理内存
    //PAGE_READWRITE 表示分配的内存区域具有读写权限
#else
	void* ptr = mmap(0, kpage << 13, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	//请求系统分配 kpage 页大小（每页 8KB）的匿名内存，
	// 该内存区域既可以读也可以写，并且是私有的，不与任何文件关联。
	// 如果映射成功，mmap 函数会返回映射区域的起始地址；
	// 如果失败，会返回 MAP_FAILED
	if (ptr == MAP_FAILED) {
		ptr = nullptr;
	}
#endif
    if(ptr==nullptr)
        throw std::bad_alloc();
    
    return ptr;
}
```

​	这里我们可以通过条件编译将对应平台下向堆申请内存的函数进行封装，此后我们就不必再关心当前所在平台，当我们需要直接向堆申请内存时直接调用我们封装后的SystemAlloc函数即可。

>定长内存池中应该包含哪些成员变量？

​	对于向堆申请到的大块内存，我们可以用一个指针来对其进行管理，但仅用一个指针肯定是不够的，我们还需要用一个变量来记录这块内存的长度。

​	由于此后我们需要将这块内存进行切分，为了方便切分操作，指向这块内存的指针最好是字符指针，因为指针的类型决定了指针向前或向后走一步有多大距离，当我们需要向后移动n个字符时，直接对字符指针进行加n操作即可。
![image-20250315161154877](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151611981.png)

​	其次，释放回来的定长内存块也需要被管理，我们可以把这些释放回来的定长内存块链接成一个链表，这里我们将管理释放回来的内存块的链表叫做自由链表，为了能找到这个自由链表，我们还需要一个指定自由链表的指针。
![image-20250315161346150](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151613218.png)

​	因此，定长内存池当中应包含三个成员变量：

- _memory：指向大块内存的指针。
- _remainBytes：大块内存切分过程中剩余字节数。
- _freeList：还回来过程中链接的自由链表的头指针。

>内存池如何管理释放的对象？

​	对于还回来的定长内存块，我们可以用自由链表将其链接起来，但我们并不需要为其专门定义链式结构，我们可以让内存块的前4个字节（32位平台）或8个字节（64位平台）作为指针，存储后面内存块的起始地址即可。

​	因此在向自由链表插入被释放的内存块时，先让该内存块的前4个字节或8个字节存储自由链表中第一个内存块的地址，然后再让_freeList 指向该内存块即可，也就是一个简单的链表头插操作。
![image-20250315162015746](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151620779.png)

​	如何让一个指针在32位平台下解引用后能向后访问4个字节，在64位平台下解引用后能向后访问8个字节？首先我们得知道，32位平台下指针的大小是4个字节，64位平台下指针的大小是8个字节。而指针指向的数据的类型，决定了指针解引用后能向后访问的空间大小，因此我们这里需要的是一个指向指针的指针，这里使用二级指针就行了。

​	当我们需要访问一个内存块的前4/8个字节时，我们就可以先将该内存块的地址先强制转为二级指针，由于二级指针存储的是一级指针的地址，二级指针解引用能向后访问一个指针的大小，因此在32位平台下访问的就是4个字节，在64位平台下访问的就是8个字节，此时我们访问到了该内存块的前4/8个字节。

```c++
void*& NextObj(void* ptr){
    //访问头部存放的4/8个字节(下一个自由空间的地址)
    return (*(void**)ptr);
}
```

​	需要注意的是，在释放对象时，我们应该显示调用该对象的析构函数清理该对象，因为该对象可能还管理着其他某些资源，如果不对其进行清理那么这些资源将无法被释放，就会导致内存泄漏。

```c++
//释放对象
void Delete(T* obj){
    //显示调用T的析构函数清理对象
    obj->~T();
    
    //将释放的对象头插到自由链表
    NextObj(obj)=_freeList;
    _freeList=obj;
}
```

>内存池如何为我们申请对象？

​	当我们申请对象时，内存池应该优先把还回来的内存块对象再次重复利用，因此如果自由链表当中有内存块的话，就直接从自由链表头删一个内存块进行返回即可。

![image-20250315163301303](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151633387.png)

​	如果自由链表当中没有内存块，那么我们就在大块内存中切出定长的内存块进行返回，当内存块切出后及时更新_memory指向的指向，以及 _remainBytes的值即可。
![image-20250315163522216](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151635296.png)

​	需要特别注意的是，由于当内存块释放时我们需要将内存块链接到自由链表当中，因此我们必须保证切出来的对象至少能够存储得下一个地址，所以当对象的大小小于当前所在平台指针的大小时，需要按指针的大小进行内存块的切分。

​	此外，当大块内存已经不足以切分出一个对象时，我们就应该调用我们封装的SystemAlloc函数，再次向堆申请一块内存空间，此时也要注意及时更新_memory指针的指向，以及 _remainBytes的值。

```c++
//申请对象
T* New(){
    T* obj=nullptr;
    
    //优先把还回来的内存块对象，再次重复利用
    if(_freeList !=nullptr){
        //从自由链表头删一个对象
        obj=(T*)_freeList;
        _freeList=NextObj(_freeList);
    }
    else{
        //保证对象能够存储的下地址
        size_t objSize=sizeof(T)<sizeof(void*)?sizeof(void*):sizeof(T);
        //剩余内存不够一个对象大小时，则重新开大块空间
        if(_remainBytes<objSize){
            _remainBytes=128*1024;
            _memory=(char*)SystemAlloc(_remainBytes>>13);
            if(_memory==nullptr){
                throw std::bad_alloc();
            }
        }
        //从大块内存中切出objSize字节的内存
        obj=(T*)_memory;
        _memory+=objSize;
        _remainBytes-=objSize;
    }
    //定位new,显示调用T的构造函数初始化
    new(obj)T;
    
    return obj;
}
```

​	需要注意的是，与释放对象时需要显示调用该对象的析构函数一样，当内存块切分出来后，我们也应该使用定位new，显示调用该对象的构造函数对其进行初始化。
>定长内存池整体代码如下：

```c++
//template<size_t N> //非类型模板参数
//class ObjectPool
//{};

//定长内存池
template<class T>
class ObjectPool {
public:
	//申请对象
	T* New() {
		T* obj = nullptr;

		//优先把换回来的内存块对象再次重复利用
		if (_freeList != nullptr) {
			//从自由链表头删一个对象
			obj = (T*)_freeList;
			_freeList = NextObj(_freeList);
		}
		else {
			//保证对象能够存储得下地址
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			//剩下内存不够一个对象大小时，则重新开大块空间
			if (_remainBytes < objSize) {
				_remainBytes = 128 * 1024;
				_memory = (char*)SystemAlloc(_remainBytes >> 13);
				if (_memory == nullptr) {
					throw std::bad_alloc();
				}
			}
			//从大块内存中切出objSize字节的内存
			obj = (T*)_memory;
			_memory += objSize;
			_remainBytes -= objSize;
		}
		//定位new，显示调用T的构造函数初始化
		new(obj)T;

		return obj;
	}
	//释放对象
	void Delete(T* obj) {
		//显示调用T的析构函数清理对象
		obj->~T();

		//将实现的对象头插到自由链表中
		NextObj(obj) = _freeList;
		_freeList = obj;
	}
private:
	char* _memory = nullptr; //指向大块内存的地址
	size_t _remainBytes = 0;//大块内存 在切分过程中剩余字节数
	void* _freeList = nullptr;//还回来过程中链接的自由链表的头指针
};
```

>性能对比

​	下面我们将实现的定长内存池和malloc/free进行性能对比，测试代码如下：

```c++
struct TreeNode {
	int _val;
	TreeNode* _left;
	TreeNode* _right;
	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

void TestObjectPool() {
	// 申请释放的轮次
	const size_t Rounds = 3;
	// 每轮申请释放多少次
	const size_t N = 1000000;
	std::vector<TreeNode*> v1;
	v1.reserve(N);

	//malloc和free
	size_t begin1 = clock();
	for (size_t j = 0; j < Rounds; ++j) {
		for (int i = 0; i < N; ++i){
			v1.push_back(new TreeNode);
		}
		for (int i = 0; i < N; ++i) {
			delete v1[i];
		}
		v1.clear();
	}
	size_t end1 = clock();

	//定长内存池
	ObjectPool<TreeNode> TNPool;
	std::vector<TreeNode*> v2;
	v2.reserve(N);
	size_t begin2 = clock();
	for (size_t j = 0; j < Rounds; ++j){
		for (int i = 0; i < N; ++i){
			v2.push_back(TNPool.New());
		}
		for (int i = 0; i < N; ++i){
			TNPool.Delete(v2[i]);
		}
		v2.clear();
	}
	size_t end2 = clock();

	std::cout << "new cost time:" << end1 - begin1 << std::endl;
	std::cout << "object pool cost time:" << end2 - begin2 << std::endl;
}
```

​	在代码中，我们先用new申请若干个TreeNode对象，然后再用delete将这些对象在释放，通过clock函数得到整个过程消耗的时间。（new和delete底层就是封装的malloc和free）

​	然后再重复过程，只不过将其中的new和delete替换为定长内存池当中的New和Delete，此时再通过clock函数得到该过程消耗的时间。

​	windows下的测试结果如下：![image-20250315172658924](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503151726969.png)

​	Linux下的测试结果如下：
![image-20250315221839844](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503152218973.png)

​	可以看到在这个过程中，不管是在windows平台下还是linux平台下，定长内存池消耗的时间比malloc/free消耗的时间要短。这就是因为malloc是一个通用的内存池，而定长内存池是专门针对申请定长对象而设计的，因此在这种特殊场景下定长内存池的效率更高，正所谓“尺有所短，寸有所长”。

## 高并发内存池整体框架设计

>该项目解决的是什么问题？

​	现代很多的开发环境都是多核多线程，因此在申请内存时，必然存在激烈的锁竞争问题。malloc本身其实已经很优秀了，但是在并发场景下可能会因为频繁的加锁和解锁导致频率有所降低，而该项目的原型tcmalloc实现的就是一种在多线程高并发场景下更胜一筹的内存池。

​	在实现内存池时我们一般需要考虑到效率问题和内存碎片的问题，但对于高并发内存池来说，我们还需要考虑在多线程环境下的锁竞争问题。

>高并发内存池整体框架设计

![image-20250315230540783](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503152305894.png)

高并发内存池主要由以下三个部分构成：

- **thread chche：**线程缓存是每个线程独有的，用于小于等于256KB的内存分配，每个线程独享一个thread cache。
- **central cache：**中心缓存是所有线程所共享的，当thread cache需要内存时会按需从central cache中获取内存，而当thread cache中的内存满足一定条件时，central cache也会在合适的时机对其进行回收。
- **page cache：**页缓存中存储的内存是以页为单位进行存储及分配的，当central cache需要内存时，page cache会分配出一定数量的页分配给central cache，而当central cache中的内存满足一定条件时，page cache也会在合适的时机对其进行回收，并将回收的内存尽可能的进行合并，组成更大的连续内存块，缓解内存碎片的问题。

**进一步说明**：

​	每个线程都有一个属于自己的thread cache，也就意味着线程在thread cache申请内存时是不需要加锁的，而一次性申请大于256KB内存的情况是很少的，因此大部分情况下申请内存都是无锁的，这也就是这个高并发内存池高效的地方。

​	每个线程的thread cache会根据自己的情况向central cache申请或归还内存 ，这就避免了出现单个线程的thread cache占用太多内存，而其余thread cache出现内存吃紧的问题。

​	多线程的thread cache可能会同时找central cache申请内存，此时就会涉及线程安全的问题，因此在访问central cache时是需要加锁的，但central cache实际上是一个哈希桶的结构，只有当多个线程同时访问同一个桶时才需要加锁，所以这里的锁竞争也不会很激烈。

>各个部分的主要作用

​	thread cache主要解决锁竞争的问题，每个线程独享自己的thread cache，当自己的thread cache中有内存时该线程不会去和其他线程进行竞争，每个线程只要在自己的thread cache申请内存就行了。

​	central cache主要起到一个居中调度的作用，每个线程的thread cache需要内存时从central cache获取，而当thread cache的内存多了就会将内存还给central cache，其作用类似于一个中枢，因此取名为中心缓存。

​	page cache就负责提供以页为单位的大块内存，当central cache需要内存时就会去向page cache申请，而当page cache没有内存了就会直接去找系统，也就是直接去堆上按页申请内存块。

## thread cache模块

### thread cache 整体设计

​	定长内存池只支持固定大小内存块的申请释放，因此定长内存池中只需要一个自由链表管理释放回来的内存块。现在我们要支持和释放不同大小的内存块，那么我们就需要多个自由链表来管理释放回来的内存块，因此thread cache实际上是一个哈希桶结构，每个桶中存放的都是一个自由链表。

​	thread cache支持小于等于256KB内存的申请，如果我们将每种字节数的内存块都用一个自由链表进行管理的话，那么此时我们就需要20多万（256*1024）个自由链表，光是存储这些自由链表的头指针就需要消耗大量内存，这显然是得不偿失的。

​	这时我们可以选择做一些平衡的牺牲，让这些字节数按照某种规则进行对齐，例如我们让这些字节数都按8字节进行向上对齐，那么thread cache的结构就是下面这样的，此时当线程申请1 ~ 8字节的内存时会直接给出8字节，而当线程申请9 ~ 16字节的内存时会直接给出16字节，以此类推。
![image-20250315235312903](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503152353023.png)

​	因此当线程要申请某一大小的内存块时，就需要经过某种计算得到对齐后的字节数，进而找到对应的哈希桶，如果该哈希桶中的自由链表中有内存块，那就从自由链接中头删一个内存块进行返回；如果该自由链表已经为空了，那么就需要向下一层的central cache进行获取了。

​	但此时由于对齐的原因，就可能会产生一些碎片化的内存无法被利用，比如线程只申请了6字节的内存，而thread cache缺直接给了8字节的内存，这多给出的2字节就无法被利用，导致了一定程度的空间浪费，这些因为某些对齐原因导致无法被利用的内存，就是内存碎片中的内部碎片。

---------

​	鉴于当前项目比较复杂，我们最好对自由链表这个结构进行封装，目前我们仅提供Push和Pop两个成员函数，对应的操作分别是将对象插入到自由链表（头插）和从自由链表中获取一个对象（头删），后面在需要时还会添加对应的成员函数。

```c++
//管理切分好的小对象的自由链表
class FreeList {
public:
	//将释放的对象头插到自由链表
	void Push(void* obj) {
		assert(obj);
		//头插
		NextObj(obj) = _freeList;
		_freeList = obj;
	}
	//从自由链表头部获取一个对象
	void* Pop() {
		assert(_freeList);
		//头删
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		return obj;
	}
private:
	void* _freeList = nullptr;//自由链表
};
```

​	因此thread cache实际上就是一个数组，数组中存储的就是一个个的自由链表，至于这个数组中到底存储了多少个自由链表，就需要看我们在进行字节数对齐时具体用的是什么映射对齐规则了。

### thread cache哈希桶映射对齐规则

>如何进行对齐？

​	上面已经说了，不是每个字节都对应一个自由链表，这样开销太大了，因此我们需要制定一个合适的映射对齐规则。首先，这些内存块是会被链接到自由链表上的，因此一开始肯定是按8字节（得保证存的下64位下的指针）进行对齐是最合适的，因为我们必须保证这些内存块，无论是在32位平台下还是64位平台下，都至少能够存储的下一个指针的大小。

​	但是如果所有的字节数都按照8字节进行对齐的话，那么我们就需要建立（256*1024）/ 8 =32768个桶，这个数量还是比较多的，实际上我们可以让不同范围的字节数按照不同的对齐数进行对齐，具体的对齐方式如下：

|          字节数          |  对齐数  | 哈希桶下标 |
| :----------------------: | :------: | :--------: |
|         [1,128]          |    8     |   [0,16)   |
|       [128+1,1024]       |    16    |  [16,72)   |
|    [1024+1,8 * 1024]     |   128    |  [72,128)  |
|  [8 * 1024+1,64 * 1024]  |   1024   | [128,184)  |
| [64 * 1024+1,256 * 1024] | 8 * 1024 | [184,208)  |

>空间浪费率

​	虽然对齐产生的内存碎片会引起一定程度的空间浪费，但按照上面的对齐规则，我们可以将浪费率控制到百分之十左右。需要说明的是，1~128这个区间我们不做讨论，因为1字节就算是对齐到2字节也有百分之五十的浪费率，这里我们就从第二个区间开始进行计算。

​	浪费率 = 浪费的字节数 / 对齐后的字节数，根据这个公式，我们要得到某个区间的最大浪费率，就应该让分子取到最大，让分母去到最小。比如129~1024这个区间，该区域的对齐数是16，那么最大浪费的字节数就是15，而最小对齐后的字节数就是这个区间内的前16个数所对齐的字节数，也就是 129+15 = 144，那么该区间的最大浪费率也就是15 / 144 = 10.42%，同样的道理，后面两个区间的最大浪费率分别是127 / 1152 = 11.02% 和 1023 / 9216 = 11.10%

>对齐和映射相关函数的编写

​	此时有了字节数的对齐规则后，我们就需要提供两个对应的函数，分别用于获取某一字节数对齐后的字节数，以及该字节数对应的哈希桶下标。关于处理对齐和映射的函数，我们可以将其封装到一个类当中。

```c++
//管理对齐和映射等关系
class SizeClass {
public:
    //获取向上对齐后的字节数
    static inline size_t RoundUp(size_t bytes);
    //获取对应哈希桶的下标
    static inline size_t Index(size_t bytes);
};
```

​	需要注意的是，SizeClass类当中的成员函数最好设置为静态成员函数，否则我们在调用这些函数时就需要通过对象去调用，并且对于这些可能会被频繁调用的函数，可以考虑将其设置为内联函数。

---

​	在获取某一字节数向上对齐后的字节数时，可以先判断该字节数属于哪一个区间，然后再通过调用一个子函数进行进一步的处理。

```c++
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
		assert(false);
		return -1;
	}
}
```

​	此时我们就需要编写一个子函数，该子函数需要通过对齐数计算出某一字节数对齐后的字节数，最容易想到的就是下面这种写法。

```c++
//一般写法
static inline size_t _RoundUp(size_t bytes, size_t alignNum) {
	size_t alignSize = 0;
	if (bytes%alignNum != 0) {
		alignSize = (bytes / alignNum + 1)*alignNum;
	}
	else {
		alignSize = bytes;
	}
	return alignSize;
}
```

​	除了这种常规写法，我们还可以通过位运算的方式来进行计算，虽然位运算可能并没有上面的写法容易理解，但计算机执行位运算的速度是比执行乘法和除法更高效的。

```c++
//位运算写法
static inline size_t _RoundUp(size_t bytes, size_t alignNum) {
	return ((bytes + alignNum - 1)& ~(alignNum - 1));
}
```

​	这个位运算理解起来也并不是很难，本质是判断当前需要对齐的数+最大的补齐数 后是否产生了进位，通过按位与运算可以保留最高位 ，即对齐后字节数。 

---

​	在获取某一个字节数对应的哈希桶下标时，也是要先判断该字节数属于哪一个区间。

```c++
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
```

​	此时在编写一个子函数处理，容易想到的就是根据对齐数来计算某一字节数对应的下标。

```c++
//一般写法
static inline size_t _Index(size_t bytes, size_t alignNum) {
	size_t index = 0;
	if (bytes%alignNum != 0) {
		index = bytes / alignNum;
	}
	else {
		index = bytes / alignNum - 1;
	}
	return index;
}
```

​	当然，为了体高效率下面也提供了一个用于位运算来解决的方法，需要注意的是，此时我们并不是传入该字节数的对齐数，而是将对齐数写成了2的n次方的形式后，将这个n值进行传入。比如对齐数是8，传入的就是3。

```c++
//位运算写法
static inline size_t _Index(size_t bytes, size_t alignShift) {
	return ((bytes + (1 << alignShift) - 1) >> alignShift) - 1;
}
```

​	这里的位运算也不难理解，本质上就是观察需要对齐的字节数+补充的最大字节数是否进位，因为哈希表的下标是从0开始的，所以在进行映射时需要减一。

---

> ThreadCache类

​	按照上述的对齐规则，thread cache中桶的个数，也就是自由链表的个数是208，以及thread cache允许申请的最大内存大小是256KB，我们可以将这些数据按照如下方式进行定义。

```c++
//小于等于MAX_BYTES，就找thread cache申请
//大于MAX_BYTES，就直接找page cache或者系统堆申请
static const size_t MAX_BYTES = 256 * 1024;
//thread cache和central cache自由链表哈希桶的表大小
static const size_t NFREELISTS = 208;
```

​	现在我们就可以对ThreadCache类进行定义了，thread cache就是一个存储208个自由链表的数组，目前thread cache就先提供一个Allocate函数用于申请对象就行了， 后面需要时在进行增加。

```c++
class ThreadCache {
public:
	//申请内存对象
	void* Allocate(size_t size);
private:
	FreeList _freeLists[NFREELISTS]; //哈希桶
};
```

​	在thread cache申请对象时，通过所给字节数计算出对应的哈希桶下标，如果桶中自由链表不为空，则从该自由链表中取出一个对象进行返回即可（头删），但如果此时自由链表为空，那么我们就需要从central cache进行获取了，这里的FetchFromCentralCache函数也是thread cache类中的一个成员函数，在后面再进行具体实现。

```c++
//申请内存对象
void* ThreadCache::Allocate(size_t size) {
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);
	size_t index = SizeClass::Index(size);
	if (!_freeLists[index].Empty()) {
		return _freeLists[index].Pop();
	}
	else {
		return FetchFromCentralCache(index, alignSize);
	}
}
```

### thread cache TLS无锁访问

​	每个线程都有一个自己独享的thread cache，那应该如何创建这个thread cache呢？我们不能将这个thread cache创建为全局的，因为当前进程的全局变量是所有线程所共享的，这也就不可避免的需要锁来控制，增加了控制成本和代码复杂度。

​	要实现每个线程无锁的访问属于自己的thread cache，我们需要用到线程局部存储TLS（Thread Local Storage），这是一种变量存储方法，使用该存储方法的变量在它所在的线程是全局可访问的，但是不能被其他线程所访问到，这样就保持了数据的线程独立性。

```c++
//TLS - Thread Local Storage
#ifdef _WIN32
    static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;
#else
    //static __thread ThreadCache* pTLSThreadCache = nullptr;
    static thread_local ThreadCache* pTLSThreadCache = nullptr;
#endif
```

​	但不是每个线程被创建时就立马有了属于自己的thread cache，而是当该线程调用相关申请内存的接口时才会创建自己的thread cache，因此在申请内存的函数中会包含以下逻辑：

```c++
//通过TLS，每个线程无锁的获取自己专属的Thread Cache对象
if(pTLSThreadCache == nullptr){
    pTLSThreadCache = new ThreadCache;
}
```

## central cache 模块

### central cache整体设计

​	当线程申请某一大小的内存时，如果thread cache中对应的自由链表不为空，那么直接取出一个内存块进行返回即可，但如果此时该自由链表为空，那么这时thread cache就需要向central cache申请内存了。

​	central cache的结构与thread cache是一样的，它们都是哈希桶的结构，并且它们遵循的对齐映射规则都是一样的。这样做的好处就是，当thread cache的某个桶中没有内存了，就可以直接到central cache中对应的哈希桶里去取内存就行了。

>central cache 与 thread cache的不同之处

​	central cache与thread cache有两个明显不同的地方，首先，thread cache是每个线程独享的，而central cache是所有线程共享的，因为每个线程的thread cache没有内存了都会取找central cache，因此在访问central cache时是需要加锁的。

​	但central cache在加锁时并不是将整个central cache全部锁上了，central cache在加锁时用的是桶锁，也就是说每个桶都有一个锁。此时只有当多个线程同时访问central cache的同一个桶时才会存在锁竞争，如果是多个线程同时访问central cache的不同桶时就不会存在锁竞争。

​	central cache与thread cache的第二个不同之处在于，thread cache的每个桶中挂的是一个个切好的内存块，而central cache的每个桶中挂的是一个个的span。
![image-20250316201800507](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503162018866.png)
​	每个span管理的都是一个以页为单位的大块内存，每个桶里面的若干span是按照双向链表的形式链接起来的，并且**每个span里面还有一个自由链表**，这个自由链表里面挂的就是一个个切好了的内存块，根据其所在的哈希桶的位置将这些内存块被切成了对应的大小。

### central cache结构设计

>页号的类型？

​	每个程序运行起来后都有自己的进程地址空间，在32位平台下，进程地址空间的大小是2^32,而在64位平台下，进程地址空间的大小是 2^64。

​	页的大小一般是4K或者8K，我们以8K为例。在32位平台下，进程地址空间就可以被分为2^32 / 2^13 = 2 ^19 个页；在64位平台下，进程地址空间就可以被分为2 ^ 64 / 2 ^ 13 = 2 ^ 51个页。页号本质是与地址一样的，它们都是一个编号，只不过地址是以一个字节为一个单位的，而页是以多个字节为一个单位的。

​	由于页号在64位平台下的取值范围是[ 0 , 2^51 )，因此我们不能简单的用一个无符号整型来存储页号，这时我们需要借助条件编译来解决这个问题。

```c++
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;
#elif defined(__x86_64__) || defined(__ppc64__)
    typedef unsigned long long PAGE_ID; // Linux 64 位系统
#else
    typedef size_t PAGE_ID; // Linux 32 位系统
#endif
```

​	需要注意的是，在32位下，\_WIN32有定义，\_WIN64没有定义；而在64位下，\_WIN32和\_WIN64都有定义。因此在条件编译时，我们应该先判断\_WIN64是否有定义，再判断_WIN32是否有定义。

> span的结构

​	central cache的每个桶里挂的是一个个的span，span是一个管理以页为单位的大块内存，span的结构如下：

```c++
//管理以页为单位的大块内存
struct Span {
	PAGE_ID _pageId = 0;        //大块内存起始页的页号
	size_t _n = 0;              //页的数量

	Span* _next = nullptr;      //双链表结构
	Span* _prev = nullptr;

	size_t _useCount = 0;       //切好的小块内存，被分配给thread cache的计数
	void* _freeList = nullptr;  //切好的小块内存的自由链表
};
```

​	对于span管理的以页为单位的大块内存，我们需要知道这块内存具体是在哪一个位置，便于之后page cache进行前后页的合并，因此span结构当中会记录所管理大块内存起始页的页号。

​	至于每一个span管理的到底是多少个页，这并不是固定的，需要根据多方面的因素来控制，因此span结构当中有一个_n 成员，该成员就代表着该span管理的页的数量。

​	此外，每个span管理的大块内存，都会被切成相应大小的内存块挂到当前span的自由链表中，比如8Byte哈希桶的span，会被切成一个个8Byte大小的内存块挂到当前span的自由链表中，因此span结构中需要存储切好的小块内存的自由链表。

​	span结构当中的_useCount成员记录的就是，当前span中切好的小块内存，被分配给thread    cache的计数，当某个span的 _useCount计数变为0时，代表当前span切出去的内存块对象全部还回来了，此时central cache就可以将这个span再还给第三层page cache。

​	每个桶当中的span是以双链表的形式组织起来的，当我们需要将某个span归还给page cache时，就可以很方便的将该span从双链表结构中移出。如果用单链表结构的话就比较麻烦了，因为单链表在删除时，需要知道当前节点的前一个结点。

>双链表结构

​	根据上面的描述，central cache的每个哈希桶里面存储的都是一个双链表结构，对于该双链表结构我们可以对其进行封装。

```c++
//带头双向循环链表
class SpanList {
public:
	SpanList() {
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
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

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;
        //不用删
	}
private:
	Span* _head;
public:
	std::mutex _mtx; //桶锁
};
```

​	需要注意的是，从双链表删除的span会还给下一层的page cache，相当于只是把这个span从双链表中移除，因此不需要对删除的span进行delete操作。

>central cache的结构

​	central cache的映射规则和thread cache是一样的，因此central cache里面哈希桶的个数页数208，但central cache每个哈希桶中存储的就是我们上面定义的双链表结构。

```c++
class CentralCache {
public:
	//...
private:
	SpanList _spanLists[NFREELISTS];
};
```

​	central cache和thread cache的映射规则一样，有一个好处就是，当thread cache的某个桶没有内存了，就可以直接去central cache对应的哈希桶进行申请就行了。

### central cache 核心实现

>central cache的实现方式

​	每个线程都有一个属于自己的thread cache，我们是用TLS来实现每个线程无锁的访问属于自己的thread cache的。而central cache和page cache在整个进程中只有一个，对于这种只能创建一个对象的类，我们可以将其设置为单例模型。

​	单例模式可以保证系统中该类只有一个实例，并提供一个访问它的全局访问点，该实例被所有程序模块共享。单例模式又分为饿汉模式和懒汉模式，懒汉模式相对比较复杂，我们这里使用饿汉模式就足够了。

```c++
//单例模式
class CentralCache {
public:
    //提供一个全局访问点
    static CentralCache* GetInstance() 
        return &_sInst;
    }
private:
	SpanList _spanLists[NFREELISTS];
private:
	CentralCache(){} //构造函数私有
	CentralCache(const CentralCache&)=delete; //防拷贝

	static CentralCache _sInst;
};
```

​	为了保证CentralCache类只能创建一个对象，我们需要将central cache的构造函数和拷贝构造函数设置为私有，或者在C++11中也可以在函数声明的后面加上 =delete 进行修饰。

​	CentralCache类当中还需要有一个CentralCache类型的静态成员变量，当程序运行起来后我们就立马创建该对象，在此后的程序中就只有这一个单例了。

```c++
CentralCache CentralCache::_sInst;
```

	最后central cache还需要提供一个共有的成员函数，用于获取该对象，此时在整个进程中就只有一个central cache对象了。
---

>慢开始反馈调节算法

​	当thread cache向central cache申请内存时，central cache应该给出多少个对象呢？这是一个值得思考的问题，如果central cache给的太少，那么thread cache在短时间内用完了又回来申请；但如果一次性给的太多了，可能thread cache永不完也就浪费了。

​	鉴于此，我们这里采用了一个慢开始反馈调节算法。当thread cache向central cache申请内存时，如果申请的是较小的对象，那么就可以多给一点，但如果申请的是较大的对象，那么就少给一点。

​	通过下面这个函数，我们就可以根据所需申请的对象的大小计算出具体给出的对象个数，并且可以将给出的对象个数控制在2~512个之间，也就是说，就算thread cache要申请的对象再小，我最多一次性给出512个对象；就算thread cache要申请的对象再大，我至少一次性给出2个对象。

```c++
//管理对齐和映射等关系
class SizeClass {
public:
	//thread cache一次从central cache获取对象的上限
	static size_t NumMoveSize(size_t size) {
		assert(size > 0);
		//对象越小，计算出的上限越高
		//对象越大，计算出的上限越低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}
};
```

​	但就算申请的是小对象，一次性给出512个也是比较多的，基于这个原因，我们可以再FreeList结构中增加一个叫做_maxSize的成员变量，该变量的初始值设置为1，并且提供一个公有成员函数用于获取这个变量。也就是说，现在thread cache中的每个自由链表都会又一个自己的 _maxSize。

```c++
//管理切分好的小对象的自由链表
class FreeList {
public:
	size_t& MaxSize() {
		return _maxSize;
	}
private:
	void* _freeList = nullptr; //自由链表
	size_t _maxSize = 1;
};
```

​	此时当thread cache申请对象时，我们会比较_maxSize和计算得出的值，取出其中的较小值作为本次申请的对象的个数。此外，如果本次采用的是 _maxSize的值，那么还会将thread cache中该自由链表中的 _maxSize的值进行加一。

​	因此，thread cache第一次向central cache申请某大小的对象时，申请到的都是一个，但下一次thread cache再向central cache申请同样大小的对象时，因为该自由链表中的_maxSize增加了，最终就会申请到两个。直到该自由链表中 _maxSize的值，增长到超过计算出的值后就不会继续增长了，此后申请到的对象的个数就是我们计算出的个数。（这里倒是有点像计算机网络当中的拥塞控制机制）

---

>从中心缓存获取对象

​	每次thread cache向central cache申请对象时，我们先通过慢开始反馈调节算法计算出本次应该申请的对象的个数，然后再向central cache进行申请。

​	如果thread cache最终申请到对象的个数就是一个，那么直接将该对象返回即可。为什么需要返回一个申请到的对象呢？因为thread cache要向central cache申请对象，其实由于某个线程向thread cache申请对象但thread cache当中暂时没有，这才导致thread cache要向central cache 申请对象。因此central cache将对象返回给thread cache后，thread cache会将该对象返回给正在申请内存的线程。

​	但如果thread cache最终申请到的是多个对象，那么除了将第一个对象返回之外，还需要将剩下的对象挂到thread cache对应的哈希桶当中。

```c++
//从中心缓存获取对象
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size) {
	//慢开始反馈调节算法
	//1、最开始不会一次向central cache一次批量要太多，因为要太多了可能用不完
	//2、如果你不断有size大小的内存需求，那么batchNum就会不断增长，直到上限
	size_t batchNum = std::min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));
	if (batchNum == _freeLists[index].MaxSize()) {
		_freeLists[index].MaxSize() += 1;
	}
	void* start = nullptr;
	void* end = nullptr;
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum >= 1); //至少有一个

	if (actualNum == 1) { //申请到对象的个数是一个，则直接将这一个对象返回即可
		assert(start == end);
		return start;
	}
	else { //申请到对象的个数是多个，还需要将剩下的对象挂到thread cache中对应的哈希桶中
		_freeLists[index].PushRange(NextObj(start), end);
		return start;
	}
}
```

---

>从中心缓存获取一定数量的对象

​	这里我们要从central cache获取n个指定大小的对象，这些对象肯定都是从central cache对应哈希桶的某个span中取出来的，因此取出来的这n个对象肯定是链接在一起的，我们只需要得到这段链表的头和尾即可，这里可以采用输出型参数进行获取。

```c++
//从central cache获取一定数量的对象给thread cache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t n, size_t size) {
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); //加锁
	
	//在对应哈希桶中获取一个非空的span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span); //span不为空
	assert(span->_freeList); //span当中的自由链表也不为空

	//从span中获取n个对象
	//如果不够n个，有多少拿多少
	start = span->_freeList;
	end = span->_freeList;
	size_t actualNum = 1;
	while (NextObj(end)&& n - 1) {
		end = NextObj(end);
		actualNum++;
		n--;
	}
	span->_freeList = NextObj(end); //取完后剩下的对象继续放到自由链表
	NextObj(end) = nullptr; //取出的一段链表的表尾置空
	span->_useCount += actualNum; //更新被分配给thread cache的计数

	_spanLists[index]._mtx.unlock(); //解锁
	return actualNum;
}
```

​	由于central cache是所有线程共享的，所以我们在访问central cache中的哈希桶时，需要先给对应的哈希桶上加上桶锁，在获取到对象后再将桶锁解掉。

​	在向central cache获取对象时，先是在central cache对应的哈希桶中获取到一个非空的span，然后从这个span的自由链表中取出n个对象即可，但可能这个非空的span的自由链表当中对象的个数不足n个，这时该自由链表当中有多少个对象就给多少就行了。

​	也就是说，thread cache实际从central cache获得的对象可能与我们传入的n值是不一样的，因此我们需要统计本次申请过程中，实际thread cache获取到的对象个数，然后根据该值及时更新这个span中的小对象被分配给thread cache的计数。

​	需要注意的是，虽然我们实际申请到对象的个数可能比n要小，但这并不会产生任何影响。因为thread cache的本意就是向central cache申请一个对象而已，我们之所以要一次多申请一些对象，是因为这样一下来下次线程再申请相同大小的对象时就可以直接在thread cache里面获取了，而不用再向central cache申请对象。

---

> 插入一段范围的对象到自由链表

​	此外，如果thread cache最终从central cache获取到的对象个数是大于一的，那么我们还需要将剩下的对象插入到thread cache中对应的哈希桶中，为了能让自由链表支持插入一段范围的对象，我们还需要在FreeList类中增加一个对应的成员函数。

```c++
//管理切分好的小对象的自由链表
class FreeList {
public:
	//插入一段范围的对象到自由链表
	void PushRange(void* start, void* end) {
		assert(start);
		assert(end);
        
		//头插
		NextObj(end) = _freeList;
		_freeList = start;
	}
private:
	void* _freeList = nullptr; //自由链表
	size_t _maxSize = 1;
};
```

---

## page cache模块

### page cache整体设计

>page cache 与central cache结构的相同之处

​	page cache与central cache一样，它们都是哈希桶结构，并且page cache的每个哈希桶里挂的也是一个个的span，这些span也是按照双链表的结构链接起来的。

---

>page cache与central cache结构的不同之处

​	首先，central cache的映射规则与thread cache保持一致，而page吗cache的映射规则与它们都不相同。page cache的哈希桶映射规则采用的是直接定址法，比如1号桶挂的都是1页的span，2号桶挂的都是2页的span，以此类推。

​	其次，central cache每个桶的span被切成了一个个对应大小的对象，以供thread cache申请。而page cache当中的span是没有被进一步切小的，因为page cache服务的是central cache，当central cache没有span时，向page cache申请的是某一固定页数的span，而如何切分申请到的这个span就应该由central cache自己来决定。
![image-20250316235039430](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503162350548.png)

​	至于page cache当中究竟有多少个桶，这就要看你最大想挂几页的span了，这里我们就最大挂128页的span，为了让桶号与页号对应起来，我们可以将第0号桶空出来不用，因此我们需要将哈希桶的个数设置为129。

```c++
//page cache中哈希桶的个数
static const size_t NPAGES = 129;
```

​	为什么这里最大挂128页的span呢？因为线程申请单个对象最大是256KB，而128页可以被切成4个256KB的对象，因此是足够的。当然，如果你想在page cache中挂更大的span也是可以的，根据具体的需求进行设置就行了。

---

>在page cache获取一个n页的span的过程

​	如果central cache要获取一个n页的span，那我们就可以在page cache的第n号桶中取出一个span返回给central cache即可，但如果第n号桶中没有span了，这时我们并不是直接转而向堆申请一个n页的span，而是要继续在后面的桶当中寻找span。

​	直接向堆申请以页为单位的内存时，我们应该尽量申请大块一点的内存块，因为此时申请到的内存是连续的，当线程需要内存时我们可以将其切小后分配给线程，而当线程将内存释放后我们又可以将其合并成大块的连续内存。如果我们向堆申请内存时是小块小块的申请的，那么我们申请到的内存就不一定是连续的了。

​	因此，当第n号桶中没有span时，我们可以继续找第n+1号桶，因为我们可以将第n+1页的span切分成一个n页的span和一个1页的span，这时我们就可以将n页的span返回，而将切分后1页的span挂到1号桶中。但如果后面的桶当中都没有span，这时我们就只能向堆申请一个128页的内存块，并将其用一个span结构管理起来，然后将128页的span切分成n页的span和128-n页的span，其中n页的span返回给central cache，而128-n页的span就挂到第128-n号桶中。

​	也就是说，我们每次向堆申请的都是128页大小的内存块，central cache要的这些span实际都是由128页的span切分出来的。

---

>page cache的实现方式

​	当每个线程的thread cache没有内存时都会向central cache申请，此时多个线程的thread cache如果访问的不是central cache的同一个桶，那么这些线程是可以同时进行访问的。这时central cache的多个桶就可能同时向page cache申请内存的，所以page cache也是存在线程安全问题的，因此在访问page cache时也必须要加锁。

​	但是在page cache这里我们不能使用桶锁，因为当central cache向page cache申请内存时，page cache可能会将其他桶当中大页的span切小后再给central cache。此外，当central cache将某个span归还给page cache时，page cache也会尝试将该span与其他桶当中的span进行合并。

​	也就是说，在访问page cache时，我们可能需要访问page cache中的多个桶，如果page cache用桶锁就会出现大量频繁的加锁和解锁，导致程序的效率低下。因此我们在访问page cache时并没有使用桶锁，而是用一个大锁将整个page cache给锁住。

​	而thread cache在访问central cache时，只需要访问central cache中对应的哈希桶就行了，因为central cache的每个哈希桶中的span都被切分成了对应大小，thread cache只需要根据自己所需对象的大小访问central cache中对应的哈希桶即可，不会访问其他哈希桶，因此central cache可以用桶锁。

​	此外，page cache在整个进程中也是只能存在一个的，因此我们也需要将其设置为单例模式。

```c++
//单例模式
class PageCache{
public:
    //提供一个全局访问点
    static PageCache* GetInstance(){
        return &_sInst;
    }
private:
    SpanList _spanLists[NPAGES];
    std::mutex _pageMtx;//一把大锁
private:
    PageCache()=delete;
    PageCache(const PageCache&)=delete;
    static PageCache _sInst;
}
```

​	当程序运行起来后我们就立马创建该对象即可。

```c++
PageCache PageCache::_sInst;
```

---

### page cache中获取Span

>获取一个非空的span

​	thread cache向central cache申请对象时，central cache需要先从对应的哈希桶中获取到一个非空的span，然后从这个非空的span中取出若干对象返回给thread cache。那central cache到底是如何从对应的哈希桶中，获取到一个非空的span的呢？

​	首先当然是先遍历central cache对应的哈希桶当中的双链表，如果该双链表中有非空的span，那么直接将该span进行返回即可。为了方便遍历这个双链表，我们可以模拟迭代器的方式，给SpanList类提供Begin和End成员函数，分别用于获取双链表中的第一个span和最后一个span的下一个位置，也就是头结点。

```c++
//带头双向循环链表
class SpanList {
public:
	Span* Begin() {
		return _head->_next;
	}
	Span* End() {
		return _head;
	}
private:
	Span* _head;
public:
	std::mutex _mtx; //桶锁
};
```

​	但如果遍历双链表后发现双链表中没有span，或该双链表中的span都为空，那么此时central cache就需要向page cache申请内存块了。

​	那具体是向page cache申请多大的内存块呢？我们可以根据具体所需对象的大小来决定，就像之前我们根据对象的大小计算出，thread cache一次向central cache申请对象的个数上限，现在我们是根据对象的大小计算出，central cache一次应该向page cache申请几页的内存块。

​	我们可以先根据对象的大小计算出，thread cache一次向central cache申请对象的个数上限，然后将这个上限值乘以单个对象大小，就算出了具体需要多少字节，最后再将这个算出来的字节转换为页数，如果转换后不够一页，那么我们就申请一页，否则转化出来是几页就申请几页。也就是说，central cache向page cache申请内存时，要求申请到的内存尽量能够满足thread cache向central cache申请时的上限。

```c++
//管理对齐和映射等关系
class SizeClass {
public:
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
```

​	代码中的PAGE_SHIFT代表页大小转换偏移，我们这里以页的大小为8K为例，PAGE_SHIFT的值就是13。

```c++
//页大小转换偏移，即一页定义为2^13，也就是8KB
static const size_t PAGE_SHIFT = 13;
```

​	需要注意的是，当central cache申请到若干页的span后，还需要将这个span切成一个个对应大小的对象挂到该span的自由链表当中。

​	如何找到一个span所管理的内存块呢？首先需要计算出该span的起始地址，我们可以用这个span的起始页号乘以一页的大小即可以得到这个span的起始地址，然后用这个span的页数乘以一页的大小就可以得到这个span锁管理的内存块的大小，用起始地址加上内存块的大小即可得到这块内存的结束地址。

​	明确了这块内存的起始和结束位置后，我们就可以进行切分了。根据所需对象的大小，每次从大块内存切出一块固定大小的内存块尾插到span的自由链表中即可。

​	为什么是尾插呢？因为我们如果是将切好的对象尾插到自由链表，这些对象看起来是按照链式结构链接起来的，而实际上它们在物理上是连续的，这时当我们把这些连续内存分配给某个线程使用时，可以提高该线程的CPU缓存利用率。

```c++
//获取一个非空的span
Span* CentralCache::GetOneSpan(SpanList& spanList,size_t size){
    //1.先在spanList中寻找非空的span
    Span* it=spanList.Begin();
    while(it != spanList.End()){
        if(it->_freeList !=nullptr){
            return it;
        }
        else{
            it=it->_next;
        }
    }
    //2.spanList中没有非空的span，只能向page cache申请
    Span* span=PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
    //计算span的大块内存的起始地址和大块内存的大小(字节数)
    char* start=(char*)(span->_pageId <<PAGE_SHIFT);
    size_t bytes=span->_n <<PAGE_SHIFT;
    
    //把大块内存切成size大小的对象链接起来
    char* end=start+bytes;
    //先切一块下来去做尾，方便尾插
    span->_freeList=start;
    start+=size;
    void* tail=span->_freeList;
    while(start<end){
        NextObj(tail)=start;
        tail=NextObj(tail);
        start+=size;
    }
    NextObj(tail)=nullptr;//尾的指向置空
    
    //将切好的span头插到spanList中
    spanList.PushFront(span);
    
    return span;
}
```

​	需要注意的是，当我们把span切好后，需要将这个切好的span挂到central cache的对应哈希桶中。因此SpanList类还需要提高一个接口，用于将一个span插入到该双链表中。这里我们选择的是头插，这样当central cache下一次从该双链表中获取非空span时，一来就能找到。

​	由于SpanList类之前实现了Insert和Begin函数，这里实现双链表头插就非常简单，直接在双链表的Begin位置进行Insert即可。

```c++
//带头双向循环链表
class SpanList{
public:
    void PushFront(Span* span){
        Insert(Begin(),span);
    }
private:
    Span* _head;
public:
    std::mutex _mtx; //桶锁
}
```

---

>获取一个k页的span

​	当我们调用上述的GetOneSpan从central cache的某个哈希桶获取一个非空的span时，如果遍历哈希桶中的双链表后发现双链表中没有span，或该双链表中的span都为空，那么此时central cache就需要向page cache申请若干页的span了，下面我们就来说说如何从page cahce获取一个k页的span。

​	因为page cache是直接按照页数进行映射的，因此我们要从page cache获取一个k页的span，就应该直接先去找page cache的第k号桶，如果第k号桶中有span，那我们直接头删一个span返回给central cache就行了。所以我们这里需要再给SpanList类添加对应的Empty和PopFront函数。

```c++
//带头双向循环链表
class SpanList{
public:
    bool Empty(){
        return _head==_head->_next;
    }
    Span* PopFront(){
        Span* front=_head->_next;
        Erase(front);
        return front;
    }
private:
    Span* _head;
public:
    std::mutex _mtx; // 桶锁
}
```

​	如果page cache的第k号桶中没有span，我们就应该继续找后面的桶，只要后面任意一个桶中有一个n页span，我们就可以将其切分成一个k页的span和一个n-k页的span，然后将切出来k页的span返回给central cache，再将n-k页的span挂到page cache的第n-k号桶即可。

​	但如果后面的桶中也都没有span，此时我们就需要向堆申请一个128页的span了，在向堆申请内存时，直接调用我们封装的SystemAlloc函数即可。

​	需要注意的是，向堆申请内存后得到的是这块内存的起始地址，此时我们需要将该地址转换为页号。由于我们向堆申请内存时都是按页进行申请的，因此我们直接将该地址除以一页的大小即可得到对应的页号。

```c++
//获取一个k页的span
Span* PageCache::NewSpan(size_t k){
    assert(k>0 && k<NPAGES);
    //先检查第k个桶里面有没有span
    if(!_spanList[k].Empty()){
        return _spanLists[k].PopFront();
    }
    //检查以下后面的桶里面有没有span，如果有可以将其进行切分
    for(size_t i=k+1;i<NPAGES;i++){
        if(!_spanLists[i].Empty()){
            Span* nSpan=_spanLists[i].PopFront();
            Span* kSpan=new Span;
            //在nSpan的头部切k页下来
            kSpan->_pageId=nSpan->_pageId;
            kSpan->_n=k;
            
            nSpan->_pageId+=k;
            nSpan->n-=k;
            
            //将剩下的挂到对应映射的位置
            _spanLists[nSpan->_n].PushFront(nSpan);
            return kSpan;
        }
    }
    //走到这里说明后面没有大页的span了，这时就像堆申请一个128页的span
    Span* bigSpan=new Span;
    void* ptr=SystemAlloc(NPAGES-1);
    bigSpan->_pageId=(PAGE_ID)ptr >>PAGE_SHIFT; //页号，根据地址来的
    bigSpan->_n=PAGES-1;
    
    _spanLists[bigSpan->_n].PushFront(bigSpan);
    
    //尽量避免代码重复，所以选择递归调用自己
    return NewSpan(k);
}
```

​	这里说明一下，当我们向堆申请到128页的span后，需要将其切分成k页的span和128-k页的span，但是为了避免出现重复的代码，我们最好不要在编写对应的切分代码。我们可以先将申请到的128页的span挂到page cache对应的哈希桶中，然后再递归调用该函数就行了，此时再往后找span的时候就一定会在第128号桶中找到该span，然后进行切分。

​	这里其实有一个问题：当central cache向page cache申请内存时，central cache对应的哈希桶是处于加锁的状态的，那么访问page cache之前我们应不应该把central cache对应的桶锁解掉呢？

​	这里建议在访问page cache前，先把central cache对应的桶锁解掉。虽然此时central cache的这个桶当中是没有内存供其他thread cache申请的，但thread cache除了申请内存还会释放内存，如果在访问page cahce之前将central cache对应的桶锁解掉，那么此时当其他thread cache想要归还内存到central cache的这个桶时就不会被阻塞。

​	因此在调用NewSpan函数之前，我们需要先将central cache对应的桶锁解掉，然后再将page cache的大锁加上，当申请到k页的span后，我们需要将page cahce的大锁解掉，但此时我们不需要立刻获取到central cache中对应的桶锁。因为central cache拿到k页的span后还会对其进行切分操作，因此我们可以在span切好后需要将其挂到central cache对应的桶上时，再获取对应的桶锁。

​	这里为了让代码清晰一点，只写出了加锁和解锁的逻辑，我们只需要将这些逻辑添加到之前实现的GetOneSpan函数的对应位置即可。

```c++
spanList._mtx.unlock();//解桶锁
PageCache::GetInstance()->_pageMtx.lock(); //加大锁
//从page cache申请k页的span
PageCache::GetInstance()->_pageMtx.unlock();//解大锁
//进行span的切分
spanList._mtx.lock();//加桶锁
//将span挂到central cache对应的哈希桶。
```

---

## 申请内存过程联调

>ConcurrentAlloc函数

​	在将thread cache、central cache以及page cache的申请流程写通之后，我们就可以向外提供一个ConcurrentAlloc函数，用于申请内存块。每个线程第一次调用该函数时会通过TLS获取到自己专属的thread cache对象，然后每个线程就可以通过自己对应的thread cache申请空间了。

```c++
static void* ConcurrentAlloc(size_t size) {
	//通过TLS，每个线程无锁的获取自己专属的ThreadCache对象
	if (pTLSThreadCache == nullptr) {
		pTLSThreadCache = new ThreadCache;
	}
	return pTLSThreadCache->Allocate(size);
}
```

>申请内存过程联调测试一

​	由于在多线程场景下调试观察起来非常的麻烦，这里在调试观察时就先不考虑多线程的场景，看看在单线程场景下代码的执行逻辑是否符合我们的预期，其次，我们这里就只简单观察下在一个桶当中的内存申请就行了。

​	下面该进程进行了三次内存申请，这三次内存申请的字节数最终都对齐到了8，此时当线程申请内存时就只会访问到thread cache的第0号桶。

```c++
void* p1 = ConcurrentAlloc(6);
void* p2 = ConcurrentAlloc(8);
void* p3 = ConcurrentAlloc(1);
```

​	当线程第一次申请内存时，该线程需要通过TLS获取到自己专属的thread cache对象，然后通过这个thread cache对象进行内存申请。
![image-20250318145736936](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181457085.png)

​	在申请内存时通过计算索引到了thread cache的第0号桶，但此时thread cache的第0号桶中是没有对象的，因此thread cache需要向central cache申请内存块。

![image-20250318151642584](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181516719.png)

​	在向central cache申请内存块前，首先通过NumMoveSize函数计算得出，thread cache一次最多可向central cache申请8字节大小的对象是512，但由于我们采用的是满开始算法，因此还需要将上限值与对应自由链表的_maxSize的值进行比较，而此时对应自由链表的 _maxSize的值是1，所以最终得出本次thread cache向central cache申请8字节对象的个数是1个。

​	并且在此之后会将该自由链表中的_maxSize的值进行自增，下一次thread cache再向central cache申请8字节对象时最终申请对象的个数就会是2个了。
![image-20250318153102783](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181531901.png)

​	在thread cache向central cache申请对象之前，需要先将central cache的0号桶的锁加上，然后再从该桶获取一个非空的span。
![image-20250318154122149](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181541306.png)

​	在central cache的第0号桶获取非空span时，先遍历对应的span双链表，看看有没有非空的span，但此时肯定是没有的，因此在这个过程中我们无法找到一个非空的span。

​	那么此时central cache就需要向page cache申请内存了，但在此之前需要先把central cache的0号桶的锁解掉，然后再将page caceh的大锁给加上，之后才能向page cache申请内存。

![image-20250318155306682](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181553786.png)

​	在向page cache申请内存时，由于central cache一次给thread cache 8字节对象的上限是512，对应的就是512*8=4096字节，所需字节数不足一页就按一页算，所以这里central cache就需要向page cache申请一页的内存块。
![image-20250318161439540](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181614684.png)

​	但此时page cache的第一个桶以及之后的桶当中都是没有span的，因此page cache需要直接向堆申请一个128页的span。
![image-20250318171523686](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181715965.png)

​	我们可以顺便验证一下，按页向堆申请的内存块的起始地址和页号之间是否是可以相互转换的。
![image-20250318171827541](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181718686.png)

​	现在将申请到的128页的span插入到page cache的128号桶当中，然后再调用一次NewSpan，在这次调用的时候，虽然在1号桶当中还是没有span，但是在往后找的过程中就一定会在128号桶找到一个span。
![image-20250318172324699](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181723831.png)

​	此时我们就可以把这个128页的span拿出来，切分成1页的span和127页的span，将1页的span返回给central cache，而把127页的span挂到page cache的第127号桶即可。
![image-20250318172714249](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181727468.png)

​	从page cache返回后，就可以把page cache的大锁解掉了，但紧接着还有将获取到的1页的span进行切分，因此这里没有立即重新加上central cache对应的桶锁。

![image-20250318173131497](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181731602.png)

​	在进行切分的时候，先通过该span的起始页号得到该span的起始地址，然后通过该span的页数得到该span所管理内存块的总的字节数。
![image-20250318174246505](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503181742598.png)

​	在确定内存块的开始和结束后，就可以将其切分成一个个8字节大小的对象挂到该span的自由链表当中了。切分出来的每个8字节的对象的8个字节【这里是linux下的64位】存储的都是下一个8字节对象的起始地址，我们可以打印前四个看看。
![image-20250318200146781](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182001880.png)

​	当切分结束后再获取central cache第0号桶的桶锁，然后将这个切好的span插入到central cache的第0号桶中，最后再将这个非空的span返回，此时就获取到了一个非空的span。
![](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182010295.png)

​	由于thread cache只向central cache申请了一个对象，因此拿到这个非空的span后，直接从这个span里面取出一个对象即可，此时该span的_useCount也由0变成了1。
![image-20250318201543445](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182015600.png)

​	由于此时thread cache实际只向central cache申请了一个内存对象，因此直接将这个对象返回给申请的线程即可。
![image-20250318202015306](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182020435.png)

---

​	当线程第二次申请内存块时就不会再创建thread cache了，因为第一次申请时就已经创建好了，此时该线程直接获取到对应的thread cache进行内存块的申请即可。
![image-20250318202500149](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182025278.png)

​	当该线程第二次申请8字节大小的对象时，此时thread cache的0号桶中还是没有对象的，因为第一次thread cache只向central cache申请了一个8字节对象，因此这次申请时还需要再向central cache申请对象。

​	这时thread cache向central cache申请对象时，thread cache的第0号桶的自由链表的_maxSize已经慢增长到2了，所以这次在向central cache申请对象时就会申请2个。如果下下次【第四次】thread cache再向central cache申请8字节大小的对象，那么central cache会一次性给thread cache3个，这就是所谓的满增长。

![image-20250318203524618](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182035776.png)

​	但由于第一次central cache向page cache申请了一页的内存块，并将其切成了1024个8字节大小的对象，因此这次thread cache向central cache申请两个8字节的对象时，central cache的第0号桶当中是有对象的，直接返回两个内存对象给thread cache即可，而不用在向page cache申请内存了。

​	但线程实际申请的只有一个8字节对象，因此thread cache除了一个对象返回之外，还需要将剩下的一个对象挂到thread cache的第0号桶当中。
![image-20250318203851212](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182038348.png)

---

​	这样一来，当线程第三次申请1字节的内存时，由于1字节对齐后也是8字节，此时thread cache也就不需要再向central cache申请内存块了，直接将第0号桶当中之前剩下的一个8字节对象返回即可。
![image-20250318204106030](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182041177.png)

---

>申请内存过程联调测试二

​	为了进一步测试代码的正确性，我们可以做这样的一个测试：让线程申请1024此8字节的对象，然后通过调试观察在第1025次申请时，central cache是否会再向page cache申请内存块。

```c++
for (size_t i = 0; i < 1024; i++)
{
	void* p1 = ConcurrentAlloc(6);
}
void* p2 = ConcurrentAlloc(6);
```

​	因为central cache第一次就是向page cache申请的一页内存，这一页内存被切成了1024个8字节大小的对象，当这1024个对象全部被申请之后，再申请8字节大小的对象时central cache当中就没有对象了， 此时就应该向page cache申请内存块。

​	通过调试我们可以看到，第1025次申请8字节大小的对象时，central cache第0号桶中的这个span的_useCount已经增加到了1024，也就是说这1024个对象都已经被线程申请了，此时central cache就需要再向page cache申请一页的span来进行切分了。
![image-20250318205327512](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182053674.png)

​	而这次central cache再向page cache申请一页的内存时，page cache就是将127页span切分成1页的span和126页的span了，然后central cache拿到这1页的span后，又会将其切分成1024块8字节大小的内存块，以供thread cache申请。慢增长最后，thread cache一次向central cache要了46个内存单位。
![image-20250318210020019](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503182100194.png)

---

## thread cache回收内存

​	当某个线程申请的对象不用了，可以将其释放给thread cache，然后thread cache将该对象插入到对应哈希桶的自由链表当中即可。

​	但是随着线程不断的释放，对应自由链表的长度也会越来越长，这些内存堆积在一个thread cache中就是一种浪费，我们应该将这些内存还给central cache，这样一来，这些内存对其他线程来说也是可申请的，因此当thread cache某个桶当中的自由链表太长时我们可以进行一些处理。

​	如果thread cache某个桶当中自由链表的长度超过它一次批量向central cache申请的对象个数，那么此时我们就要把该自由链表当中的这些对象还给central cache

```c++
//释放内存对象
void ThreadCache::Deallocate(void* ptr, size_t size) {
	assert(ptr);
	assert(size <= MAX_BYTES);

	//找出对应的自由链表桶将对象插入
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	//当自由链表长度大于一次批量申请的对象个数时就开始还一段list给central cache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize()) {
		ListTooLong(_freeLists[index], size);
	}
}
```

​	当自由链表的长度大于一次批量申请的对象时，我们具体的做法就是，从该自由链表中取出一次批量个数的对象，然后将取出的这些对象还给central cache中对应的span即可。

```c++
//释放对象导致链表过长，回收内存到中心缓存
void ThreadCache::ListTooLong(FreeList& list, size_t size) {
	void* start = nullptr;
	void* end = nullptr;
	//从list中取出一次批量个数的对象
	list.PopRange(start, end, list.MaxSize());
	
	//将取出的对象还给central cache中对应的span
	CentralCache::GetInstance()->ReleaseListToSpans(start, size);
}
```

​	从上述代码可以看出，FreeList类需要支持用Size函数获取自由链表中对象的个数，还需要支持用PopRange函数从自由链表中取出指定个数的对象。因此我们需要给FreeList类增加一个对应的PopRange函数，然后再增加一个_size成员变量，该成员变量用于记录当前自由链表中对象的个数，当我们向自由链表插入或删除对象时，都应该更新 _size的值。

```c++
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
	void PushRange(void* start, void* end, size_t n) {
		assert(start);
		assert(end);

		//头插
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}
	//从自由链表获取一段范围的对象
	void PopRange(void*& start, void*& end, size_t n) {
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
	void* _freeList = nullptr; //自由链表
	size_t _maxSize = 1;
	size_t _size = 0;
};
```

​	而对于FreeList类当中的PushRange成员函数，我们最好也像PopRange一样给它增加一个参数，表示插入对象的个数，不然我们这时还需要通过遍历统计插入对象的个数。

​	因此之前在调用PushRange的地方就需要修改一下，而我们实际就在一个地方调用过PushRange函数，并且此时插入对象的个数也是很容易知道的。当时thread cache从central cache获取了actualNum个内存对象，将其中的一个返回给了申请对象的线程，剩下的actualNum-1个挂到了thread cache对应的桶当中，所以这里插入对象的个数就是actualNum-1。

```c++
_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
```

**说明一下：**

​	当thread cache的某个自由链表过长时，我们实际就是把这个自由链表当中全部的对象都还给central cache了，但这里在设计PopRange接口时还是设计的是取出指定个数的对象，因为在某些情况下当自由链表过长时，我们可能并不一定想把链表中全部的对象都取出来还给central cache，这样设计就是为了增加代码的可修改性。

​	其次，当我们判断thread cache是否以应该归还对象给central cache时，还可以综合考虑每个thread cache整体的大小。比如当某个thread cache的总占用大小超过一定阈值时，我们就将该thread cache当中的对象还一些给central cache，这样就尽量避免了某个线程的thread cache占用太多的内存。对于这一点，在tcmalloc当中就是考虑到了的。

---

## central cache回收内存

​	当thread cache中某个自由链表太长时，会将自由链表当中的这些对象还给central cache中的span。

​	但是需要注意的是，还给central cache的这些对象不一定都是属于同一个span的，central cache中的每个哈希桶当中可能不止一个span，因此当我们计算出还回来的对象应该还给central cache的哪一个桶后，还需要知道这些对象到底应该还给这个桶当中的哪一个span。

---

>如何根据对象的地址得到对象所在的页号？

​	首先我们必须理解的是，某个页当中的所有地址除以页的大小都等于该页的页号。比如我们这里假设一页的大小是100，那么地址0~99都属于第0页，它们除以100都会等于0，而地址100 ~199都属于第1页，它们除以100都等于1。

---

>如何找到一个对象对应的span？

​	虽然我们现在可以通过对象的地址得到其所在的页号，但我们还是不能知道这个对象到底是属于哪个span，因为一个span管理的可能是多个页。

​	为了解决这个问题，我们可以建立页号和span之间的映射。由于这个映射关系在page cache进行span的合并时也需要用到，因此我们直接将其存放到page cache里面。这时我们就需要在PageCache类当中添加一个映射关系了，这里可以用C++当中的unordered_map进行实现，并且添加一个函数接口，用于让central cache获取这里的映射关系。（下面代码中只展示了PageCache类当中新增的成员）

```c++
//单例模式
class PageCache {
public:
	//获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);
private:
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;
};
```

​	每当page cache分配span给central cache时，都需要记录一下页号和span之间的映射关系。此后当thread cache还对象给central cache时，才知道应该具体还给哪一个span。

​	因此当central cache在调用NewSpan接口向page cache申请k页的span时，page cache在返回这个k页的span给central cache之前，应该建立这k个页号与该span之间的映射关系。

```c++
//获取一个k页的span
Span* PageCache::NewSpan(size_t k) {
	assert(k > 0 && k < NPAGES);
	//先检查第k个桶里面有没有span
	if (!_spanLists[k].Empty()) {
		Span* kSpan = _spanLists[k].PopFront();

		//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++) {
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}

		return kSpan;
	}
	//检查一下后面的桶里面有没有span，如果有可以将其进行切分
	for (size_t i = k + 1; i < NPAGES; i++) {
		if (!_spanLists[i].Empty()) {
			Span* nSpan = _spanLists[i].PopFront();
			Span* kSpan = new Span;
			//在nSpan的头部切k页下来
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;
			//将剩下的挂到对应映射的位置
			_spanLists[nSpan->_n].PushFront(nSpan);

			//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++) {
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}
			return kSpan;
		}
	}
	//走到这里说明后面没有大页的span了，这时就向堆申请一个128页的span
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);
	//尽量避免代码重复，递归调用自己
	return NewSpan(k);
}
```

​	此时我们就可以通过对象的地址找到该对象对应的span了，直接将该对象的地址除以页的大小得到页号，然后在unordered_map当中找到其对应的span即可。

```c++
//获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj) {
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; //页号
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end()) {
		return ret->second;
	}
	else {
		assert(false);
		return nullptr;
	}
}
```

​	注意一下，当我们要通过某个页号查找其对应的span时，该页号与其span之间的映射一定是建立过的，如果此时我们没有在unordered_map当中找到，则说明我们之前的代码逻辑是有问题的，因此当没有找到对应的span时我们可以直接使用断言来结束程序，以表明程序逻辑出错。

>central cache回收内存

​	这时当thread cache还对象给central cache时，就可以一次遍历这些对象，将这些对象插入到其对应span的自由链表当中，并且及时更新该span的_useCount计数即可。

​	在thread cache还对象给central cache的过程中，如果central cache中的某个span的_useCount减到0时，说明这个span分配出去的对象全部都换回来了，那么此时就可以把这个span再进一步的还给page cache。

```c++
//将一定数量的对象还给对应的span
void CentralCache::ReleaseListToSpans(void* start, size_t size) {
	size_t index = SizeClass::Index(size);
	_spanLists[index]._mtx.lock(); //加锁
	while (start) {
		void* next = NextObj(start); //记录下一个
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		//将对象头插到span的自由链表
		NextObj(start) = span->_freeList;
		span->_freeList = start;

		span->_useCount--; //更新被分配给thread cache的计数
		if (span->_useCount == 0) {
            //说明这个span分配出去的对象全部都回来了
		
			//此时这个span就可以再回收给page cache，page cache可以再尝试去做前后页的合并
			_spanLists[index].Erase(span);
			span->_freeList = nullptr; //自由链表置空
			span->_next = nullptr;
			span->_prev = nullptr;

			//释放span给page cache时，使用page cache的锁就可以了，这时把桶锁解掉
			_spanLists[index]._mtx.unlock(); //解桶锁
			PageCache::GetInstance()->_pageMtx.lock(); //加大锁
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock(); //解大锁
			_spanLists[index]._mtx.lock(); //加桶锁
		}
		start = next;
	}
	_spanLists[index]._mtx.unlock(); //解锁
}
```

​	需要注意，如果要把某个span还给page cache，我们需要先将这个span从central cache对应的双链表中移除，然后再将该span的自由链表置空，因为page cache中的span是不需要切成一个个的小内存的，以及该span的前后指针也都应该置空，因为之后要将其插入到page cache对应的双链表中。但span当中记录的起始页号以及它管理的页数是不能清楚的，否则对应的内存块就找不到了。

​	并且在central cache还span给page cache时也存在锁的问题，此时需要先将central cache中的对应的桶锁解掉，然后再加上page cache的大锁之后才能进入page cache进行相关操作，当处理完毕回到central cache时，除了将page cache的大锁解掉，还需要立刻获得central cache对应的桶锁，目的是为了将还未还完的对象继续还给central cache中对应的span。

---

## page cache回收内存

​	如果central cache中某个span的_useCount减到0了，那么central cache就能需要将这个span还给page cache了。

​	这个过程看似是非常简单的，page cache只需将换回来的span挂到对应的哈希桶上就行了。但实际为了缓解内存碎片的问题，page cache还需要尝试将还回来的span与其他空闲的span进行合并。

---

>page cache进行前后页的合并

​	合并的过程可以分为向前合并和向后合并。如果换回来的span的起始页号是num，该span所管理的页数是n。那么在向前合并时，就需要判断第num-1页对应的span是否空闲，如果空闲则可以将其进行合并，并且合并后还需要继续向前尝试进行合并，直到不能进行合并为止。而在向后合并时，就需要判断第num+n页对应的span是否空闲，如果空闲则可以将其进行合并，并且合并后还需要继续向后尝试进行合并，直到不能进行合并为止。

​	因此page cache在合并span时，是需要通过页号获取到对应的span的，这就是我们要把页号与span之间的映射关系存储到page cache的原因。

​	但需要注意的是，当我们通过页号找到对应的span时，这个span此时可能正挂在page cache，也可能挂在central cache。而在合并时我们只能合并挂在page cache的span，因为挂载central cache的span当中的对象正在被其他线程使用。

​	可是我们不能通过span结构当中的_useCount成员来判断某个span到底是在central cache还是在page cache。因为当central cache刚向page cache申请到一个span时，这个span的 _useCount就是等于0的，这时可能当我们正在对该span进行切分的时候，page cache就把这个span拿去进行合并了，这显然是不合理的。

​	鉴于此，我们可以在span结构中再增加一个_isUse成员，用于标记这个span是否正在被使用，而当一个span结构被创建时我们默认该span是没有被使用的。

```c++
//管理以页为单位的大块内存
struct Span {
	PAGE_ID _pageId = 0;        //大块内存起始页的页号
	size_t _n = 0;              //页的数量

	Span* _next = nullptr;      //双链表结构
	Span* _prev = nullptr;

	size_t _useCount = 0;       //切好的小块内存，被分配给thread cache的计数
	void* _freeList = nullptr;  //切好的小块内存的自由链表

	bool _isUse = false;        //是否在被使用
};
```

​	因此当central cache向page cache申请到一个span时，需要立即将该span的_isUse改为true。

```c++
Span* CentralCache::GetOneSpan(SpanList& spanList, size_t size){
	span->_isUse = true;
}
```

​	而当central cache将某个span还给page cache时，也就需要将该span的_isUse改成false。

```c++
void PageCache::ReleaseSpanToPageCache(Span* span){
	span->_isUse = false;
}
```

​	由于再合并page cache当中的span时，需要通过页号找到其对应的span，而一个span是在被分配给central cache时，才建立的各个页号与span之间的映射关系，因此page cache当中的span也需要建立页号与span之间的映射关系。

​	与central cache中的span不同的是，在page cache中，只需要建立一个span的首尾页号与该span之间的映射关系。因为当一个span在尝试进行合并时，如果是往前合并，那么只需要通过一个span的尾页找到这个span，如果是向后合并，那么只需要通过一个span的首页找到这个span。也就是说，在进行合并时我们只需要用到span与其首尾页之间的映射关系就够了。

​	因此当我们申请k页的span时，如果是将n页的span切成了一个k页的span和一个n-k页的span，我们除了需要建立k页的span中每个页与该span之间的映射关系之外，还需要建立剩下的n-k页的span与其首位页之间的映射关系。

```c++
//获取一个k页的span
Span* PageCache::NewSpan(size_t k) {
	assert(k > 0 && k < NPAGES);
	//先检查第k个桶里面有没有span
	if (!_spanLists[k].Empty()) {
		Span* kSpan = _spanLists[k].PopFront();
		//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++) {
			_idSpanMap[kSpan->_pageId + i] = kSpan;
		}
		return kSpan;
	}
	//检查一下后面的桶里面有没有span，如果有可以将其进行切分
	for (size_t i = k + 1; i < NPAGES; i++) {
		if (!_spanLists[i].Empty()) {
			Span* nSpan = _spanLists[i].PopFront();
			Span* kSpan = new Span;
			//在nSpan的头部切k页下来
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;

			nSpan->_pageId += k;
			nSpan->_n -= k;
			//将剩下的挂到对应映射的位置
			_spanLists[nSpan->_n].PushFront(nSpan);
			//存储nSpan的首尾页号与nSpan之间的映射，方便page cache合并span时进行前后页的查找
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			//建立页号与span的映射，方便central cache回收小块内存时查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++) {
				_idSpanMap[kSpan->_pageId + i] = kSpan;
			}

			return kSpan;
		}
	}
	//走到这里说明后面没有大页的span了，这时就向堆申请一个128页的span
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	_spanLists[bigSpan->_n].PushFront(bigSpan);

	//尽量避免代码重复，递归调用自己
	return NewSpan(k);
}
```

​	此时page cache当中的span就都与其首位页之间建立了映射关系，现在我们就可以进行span的合并了，其合并的逻辑如下：

```c++
//释放空闲的span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span) {
	//对span的前后页，尝试进行合并，缓解内存碎片问题
	//1、向前合并
	while (1) {
		PAGE_ID prevId = span->_pageId - 1;
		auto ret = _idSpanMap.find(prevId);
		//前面的页号没有（还未向系统申请），停止向前合并
		if (ret == _idSpanMap.end()) {
			break;
		}
		//前面的页号对应的span正在被使用，停止向前合并
		Span* prevSpan = ret->second;
		if (prevSpan->_isUse == true) {
			break;
		}
		//合并出超过128页的span无法进行管理，停止向前合并
		if (prevSpan->_n + span->_n > NPAGES - 1) {
			break;
		}
		//进行向前合并
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;

		//将prevSpan从对应的双链表中移除
		_spanLists[prevSpan->_n].Erase(prevSpan);

		delete prevSpan;
	}
	//2、向后合并
	while (1) {
		PAGE_ID nextId = span->_pageId + span->_n;
		auto ret = _idSpanMap.find(nextId);
		//后面的页号没有（还未向系统申请），停止向后合并
		if (ret == _idSpanMap.end()) {
			break;
		}
		//后面的页号对应的span正在被使用，停止向后合并
		Span* nextSpan = ret->second;
		if (nextSpan->_isUse == true) {
			break;
		}
		//合并出超过128页的span无法进行管理，停止向后合并
		if (nextSpan->_n + span->_n > NPAGES - 1) {
			break;
		}
		//进行向后合并
		span->_n += nextSpan->_n;

		//将nextSpan从对应的双链表中移除
		_spanLists[nextSpan->_n].Erase(nextSpan);

		delete nextSpan;
	}
	//将合并后的span挂到对应的双链表当中
	_spanLists[span->_n].PushFront(span);
	//建立该span与其首尾页的映射
	_idSpanMap[span->_pageId] = span;
	_idSpanMap[span->_pageId + span->_n - 1] = span;
	//将该span设置为未被使用的状态
	span->_isUse = false;
}
```

需要注意的是，在向前或者向后进行合并的过程中：

- 如果没有通过页号获取到其对应的span，则说明该页的内存块还未申请，此时需要停止合并。
- 如果通过页号获取到了其对应的span，但该span处于被使用的状态，那我们也必须停止合并。
- 如果合并后大于128页则不能进行本次合并，因此page cache无法对大于128页的span进行管理。

​	在合并span时，由于这个span是在page cache的某个哈希桶的双链表当中的，因此在合并后需要将其从对应的双链表当中移除，然后再将这个被合并了的span结构进行delete。

​	除此之外，在合并结束后，除了被合并后的span挂到page cache对应哈希桶的双链表当中，还需要建立该span与其首位页之间的映射关系，便于此后合并出更大的span。

---

## 释放内存过程联调

>CoucurrentFree函数

​	至此我们将thread caceh，central cache以及page cache的释放流程也都写完了，此时我们就可以向外提供一个ConcurrentFree函数，用于释放内存块，释放内存块时每个线程通过自己的thread cache对象，调用thread cache中释放内存对象的接口即可。

```c++
static void ConcurrentFree(void* ptr, size_t size/*暂时*/) {
	assert(pTLSThreadCache);
	pTLSThreadCache->Deallocate(ptr, size);
}
```

>释放内存过程联调测试

​	之前我们在测试申请流程时，让单个线程进行了三次内存申请，现在我们再让这三个对象再进行释放，看看这其中的释放流程是如何进行的。

```c++
void* p1 = ConcurrentAlloc(6);
void* p2 = ConcurrentAlloc(8);
void* p3 = ConcurrentAlloc(1);

ConcurrentFree(p1, 6);
ConcurrentFree(p2, 8);
ConcurrentFree(p3, 1);
```

​	首先，这三个申请和释放的对象大小进行对齐后都是8字节，因此对应操作的就是thread cache和central cache的第0号桶，以及page cache的第1号桶。

​	由于第三次对象申请时，刚好将thread cache的第0号桶当中仅剩的一个对象拿走了【第一次申请拿到一个，第二次申请拿到了两个，第三次申请刚刚好够用】，因此再三次对象申请后thread cache的第0号桶当中是没有对象的。

​	通过调试可以看到，此时thread cache的第0号桶中自由链表的_maxSize以及慢慢增长到了3，而当我们释放完第一个对象后，该自由链表当中对象的个数只有一个，因此不会将该自由链表当中的对象进一步还给central cache。
![image-20250319110724122](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191107353.png)

​	当第二个对象释放给thread cache的第0号桶后，该桶对象的自由链表中对象的个数变成了2，也是不会进行ListTooLong操作的。

​	直到第三个对象释放给thread cache的第0号桶时，此时该自由链表的_size的值变为3，与 _maxSize的值相等，现在thread cache就需要将对象给central cache了。
![image-20250319111112213](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191111315.png)

​	thread cache先是将第0号桶当中的对象弹出MaxSize个，在这里实际上就是全部弹出，此时该自由链表_size的值变为0，然后继续调用central cache当中的rReleaseListToSpans函数，将这三个对象还给central cache当中对应的span。

![image-20250319111454382](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191114517.png)

​	在进行central cache的第0号桶还对象之前，先把第0号桶对应的桶锁加上，然后通过差page cache中的映射表找到其对应的span，最好将这个对象头插到span的自由链表当中，并将该span的_useCount进行- -。当第一个对象还给其对应的span时，可以看到该span的 _useCount减到了2。
![image-20250319112006990](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191120098.png)

​	而由于我们只进行了三次对象申请，并且这些对象大小对齐后大小都是8字节，因此我们申请的这三个对象实际都是对同一个span切分出来的。当我们将这三个对象都还给span时，该span的_useCount就减为了0。

![image-20250319112421380](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191124662.png)

​	现在central cache就需要将这个span进一步的还给page cache，而在将该span交给page cache之前，会将该span的自由链表以及前后指针都置空。并且在进入page cache之前会先将central cache第0号桶的锁解掉，然后再加上page cache的大锁，之后才能进入page cache进行相关操作。
![image-20250319113637478](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191136657.png)

​	由于这个一页的span是从128页的span的头部切下来的，在向前合并时由于前面的页还未向系统申请，因此在查映射关系时是无法找到的，此时直接停止了向前合并。
![image-20250319113913659](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191139831.png)

​	而在向后合并时，由于page cache没有将该页后面的页分配给central cache，因此在向后合并时肯定能够找到一个127页的span进行合并，合并后就变成了一个128页的span，这时我们将原来127页的span从第127号桶删除，然后还需要将这127页的span结构进行delete，因为它管理的127页以及和1页的span进行合并了，不再需要它来管理了。
![image-20250319114357129](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191143535.png)

​	紧接着将这个128页的span插入到第128号桶，然后建立该span与其首位页的映射，便于下次被用于合并，最后再将该span的状态设置为未被使用的状态即可。
![image-20250319114602238](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191146341.png)

​	当从page cache回来后，除了将page cache的大锁解掉，还需要立刻加上central cache中对应的桶锁，然后继续将对象还给central cache中的span，但此时实际上是换完了，因此再将central cache的桶锁解掉就行了。
![image-20250319114901693](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191149759.png)

​	至此我们便完成了这三个对象的申请和释放流程。

---

## 大于256KB的大块内存申请问题

>申请过程

​	之前说到，每个线程的thread cache是用于申请小于等于256KB的内存的，而对于大于256KB的内存，我们可以考虑直接向page cache申请，但page cache中最大的页也就只有128页，因此如果是大于128页的内存申请，就只能直接向堆申请了。

|  申请内存的大小  |      申请方式      |
| :--------------: | :----------------: |
| x<=256KB（32页） | 向thread cache申请 |
|   32页<x<128页   |  向page cache申请  |
|     x>=128页     |      向堆申请      |

​	当申请的内存大于256KB时，虽然不是从thread cache进行获取，但在分配内存时也是需要进行向上对齐的，对于大于256KB的内存我们可以直接按页进行对齐。

​	而我们之前实现RoundUp函数，对传入字节数大于256KB的情况直接做了断言处理，因此这里需要对RoundUp函数稍作修改。

```c++
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
		//大于256KB的按页对齐
		return _RoundUp(bytes, 1 << PAGE_SHIFT);
	}
}
```

​	现在对于之前的申请逻辑就需要进行修改了，当申请对象的大小大于256KB时，就不用向thread cache申请了，这时先计算出按页对齐后实际需要申请的页数，然后通过调用NewSpan申请指定页数的span即可。

```c++
static void* ConcurrentAlloc(size_t size) {
	if (size > MAX_BYTES) {
        //大于256KB的内存申请
	
		//计算出对齐后需要申请的页数
		size_t alignSize = SizeClass::RoundUp(size);
		size_t kPage = alignSize >> PAGE_SHIFT;

		//向page cache申请kPage页的span
		PageCache::GetInstance()->_pageMtx.lock();
		Span* span = PageCache::GetInstance()->NewSpan(kPage);
		PageCache::GetInstance()->_pageMtx.unlock();

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else {
		//通过TLS，每个线程无锁的获取自己专属的ThreadCache对象
		if (pTLSThreadCache == nullptr) {
			pTLSThreadCache = new ThreadCache;
		}
		cout << std::this_thread::get_id() << ":" << pTLSThreadCache << endl;

		return pTLSThreadCache->Allocate(size);
	}
}
```

​	也就是说，申请大于256KB的内存时，会直接调用page cache当中的NewSpan函数进行申请，因此这里我们需要再对NewSpan函数进行改造，当需要申请的内存页数大于128页的时，就直接向对申请对于页数的内存块，而如果申请的内存页数是小于128页的，那就在page cache中进行申请，因此当申请大于256KB的内存调用NewSpan函数时页数需要加锁的，因为我们可能是在page cache中进行申请的。

---

>释放过程

​	当释放对象时，我们需要判断释放对象的大小：

|  释放内存的大小  |      释放方式      |
| :--------------: | :----------------: |
| x<=256KB（32页） | 释放给thread cache |
|  32页<x<=128页   |  释放给page cache  |
|     x>=128页     |      释放给堆      |

​	因此当释放对象时，我们需要先找到该对象对应的span，但是在释放对象时我们只知道该对象的起始地址。这也就是我们在申请大于256KB的内存时，也要给申请到的内存建立span结构，并建立起始页号与该span之间的映射关系的原因。此时我们就可以通过释放对象的起始地址计算出起始页号，进而通过页号找到该对象对应的span。

```c++
static void ConcurrentFree(void* ptr, size_t size) {
	if (size > MAX_BYTES) { 
        //大于256KB的内存释放
		Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);

		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else {
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}
```

​	因此page cache在回收span时也需要进行判断，如果该span的大小是小于等于128页的，那么直接还给page cache就行了，page cache会尝试对其进行合并。而如果该span的大小是大于128页的，那么说明该span是直接向堆申请的，我们直接将这块内存释放给堆，然后将这个span结构进行delete就行了。

```c++
//释放空闲的span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span){
    //对span的前后页，尝试进行合并，缓解内存碎片的问题
    //1.向前合并
    
    if(span->_n >NPAGES-1){
        //大于128页直接释放给堆
        void* ptr=(void*)(span->_pageId<<PAGE_SHIFT);
        SystemFree(ptr);
        delete span;
        return;
    }
    //对span的前后页，尝试进行合并缓解内存碎片问题
    //1.向前合并
    while(1){
        PAGE_ID prevId=span->_pageId-1;
        auto ret=_idSpanMap.find(prevId);
        //前面的页号没有(还没有向系统申请)，则停止向前合并
        if(ret==_idSpanMap.end()){
            break;
        }
        //前面的页号对应的span正在被使用，停止向前合并
        Span* prevSpan=ret->second;
        if(prevSpan->_isUse==true){
            break;
        }
        //合并超过1128页的span无法进行管理，停止向前合并
        if(prevSpan->_n+span->_n>NPAGES-1){
            break;
        }
        //进行向前合并
        span->_pageId=prevSpan->_pageId;
        span->_n+=prevSpan->_n;

        //将preSpan从对应的双链表中移除
        _spanLists[prevSpan->_n].Erase(prevSpan);

        delete prevSpan;
    }
    //2.向后合并
    while(1){
        PAGE_ID nextId=span->_pageId+span->_n;
        auto ret=_idSpanMap.find(nextId);
        //后面的页号还没有(还未向系统申请),停止向后合并
        if(ret==_idSpanMap.end()){
            break;
        }
        //后面的页号对应的span正在被使用，停止向后合并
        Span* nextSpan=ret->second;
        if(nextSpan->_isUse==true){
            break;
        }
        //合并超过128页的span无法进行管理，停止向后合并
        if(nextSpan->_n+span->_n>NPAGES-1){
            break;
        }
        //进行向后合并
        span->_n+=nextSpan->_n;
        cout<<"span->_n: "<<span->_n<<endl;
        cout<<"span->_pageId: "<<span->_pageId<<endl;
        //exit(0);

        //将nextSpan从对应的双链表中移除
        _spanLists[nextSpan->_n].Erase(nextSpan);

        delete nextSpan;
    }
    //将合并后的span挂到对应的双链表当中
    _spanLists[span->_n].PushFront(span);
    //建立该span与其尾页的映射
    _idSpanMap[span->_pageId]=span;
    _idSpanMap[span->_pageId+span->_n-1]=span;
    //将该span设置为未使用的状态
    span->_isUse=false;
}
```

​	说明一下，直接向堆申请内存时我们调用的接口是VirtualAlloc，与之对应的将内存释放给堆的接口叫做VirtualFree，而Linux下的brk和mmap对应的释放接口叫做sbrk和unmmap。此时我们也可以将这些释放接口封装成一个叫做SystemFree的接口，当我们需要将内存释放给堆时直接调用SystemFree即可。

```c++
//直接将内存还给堆
inline static void SystemFree(void* ptr) {
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	munmap(ptr, 0);
#endif
}
```

---

>简单测试

​	下面我们对大于256KB的申请释放流程进行简单的测试：

```c++
//找page cache申请
void* p1 = ConcurrentAlloc(257 * 1024); //257KB
ConcurrentFree(p1, 257 * 1024);

//找堆申请
void* p2 = ConcurrentAlloc(129 * 8 * 1024); //129页
ConcurrentFree(p2, 129 * 8 * 1024);
```

​	当申请257KB的内存时，由于257KB的按页向上对其后是33页，并没有大于128页，因此不会直接向堆进行申请，会向page cache申请内存，但此时page cache当中实际是没有内存的，最终page cache就会向堆申请一个128页的span，将其切分成33页的span和95页的span，并将33页的span进行返回。
![image-20250319173256547](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191733678.png)

​	而在释放内存时，由于该对象的大小大于了256KB，因此不会将其还给thread cache，而是直接调用的page cache当中的释放接口。
![image-20250319173425080](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191734231.png)

​	由于该对象的大小是33页，不大于128页，因此page cache也不会直接将该对象还给堆，而是尝试对其进行合并，最终就会把这个33页的span和之前剩下的95页的span进行合并，最终将合并后的128页的span挂到第128号桶中。
![image-20250319173627369](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191736491.png)

---

​	当申请129页的内存时，由于是大于256KB的，于是还是调用的page cache对应的申请接口，但此时申请的内存同时页大于128页，因此会直接向堆申请。在申请后还会建立该span与其起始页之间的映射，便于释放时可以通过页号找到该span。
![image-20250319174038409](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191740586.png)

​	在释放内存时，通过对象的地址找到其对应的span，从span结构中得到释放内存的大小大于128页，于是会将该内存直接还给堆。
![image-20250319174230579](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503191742727.png)

---

## 使用定长内存池配合脱离使用new

​	tcmalloc是要在高并发场景下替代malloc进行内存申请的，因此tcmalloc在实现的时，其内部是不能调用malloc函数的，我们当前的代码存在通过new获取到的内存，而new在底层实际上就是封装了malloc。

​	为了完全脱离掉malloc函数，此时我们之前实现的定长内存池就起作用了，代码中使用new时基本都是为Span结构的对象申请空间，而span对象基本都是在page cache层创建的，因此我们可以在PageCache类当中定义一个_spanPool，用于span对象的申请和释放。

```c++
//单例模式
class PageCache {
public:
	//...
private:
	ObjectPool<Span> _spanPool;
};
```

​	然后将代码中使用new的地方都替换为调用定长内存池当中的New函数，将代码中使用delete的地方替换为调用定成内存池当中的Delete函数。

```c++
//申请span对象
Span* span = _spanPool.New();
//释放span对象
_spanPool.Delete(span);
```

​	注意，当使用定成内存池当中的New函数申请Span对象时，New函数通过定位new也是对Span对象进行了初始化的。

---

​	此外，每个线程第一次申请内存时都会创建其专属的thread cache，而这个thread cache目前也是new出来的，我们也需要对其进行替换。

```c++
//通过TLS，每个线程无锁的获取自己专属的ThreadCache对象
if (pTLSThreadCache == nullptr) {
	static std::mutex tcMtx;
	static ObjectPool<ThreadCache> tcPool;
	tcMtx.lock();
	pTLSThreadCache = tcPool.New();
	tcMtx.unlock();
}
```

​	这里我们将用于申请ThreadCache类对象的定长内存池定义为静态的，保持全局只有一个，让所有线程创建自己的thread cache时，都在一个定长内存池中申请内存就行了。

​	但注意在从该定长内存池中申请内存时需要加锁，防止多个线程同时申请自己的ThreadCache对象而导致线程安全问题。

---

​	最后在SpanList的构造函数中也用到了new，因为SpanList的带头循环双向链表，所以在构造期间我们需要申请一个span对象作为双链表的头结点。

```c++
//带头双向循环链表
class SpanList {
public:
	SpanList() {
		_head = _spanPool.New();
		_head->_next = _head;
		_head->_prev = _head;
	}
private:
	Span* _head;
	static ObjectPool<Span> _spanPool;
};
```

​	由于每个span双链表只需要一个头结点，因此将这个定长内存池定义为静态时，保持全局只有一个，让所有span双链表在申请头结点时，都在一个定长内存池中申请内存就行了。

---

## 释放对象时优化为不传对象大小

​	当我们使用malloc函数申请内存时，需要指明申请内存的大小；而当我们使用free函数释放内存时，只需要传入指定这块内存的指针即可。

​	而我们目前实现的内存池，在释放对象时除了需要传入指向该对象的指针，还需要传入该对象的大小。

原因如下：

- 如果释放的是大于256KB的对象，需要根据对象的大小来判断这块内存到底应该还给page cache，还是应该直接还给堆。
- 如果释放的是小于等于256KB的对象，需要根据对象的大小计算出应该还给thread cache的哪一个哈希桶。

​	如果我们也想做到，在释放对象时不用传入对象的大小、那么我们就需要建立对象地址与对象大小之间的映射。由于现在可以通过对象的地址找到其对应的span，而span的自由链表中挂的都是相同大小的对象。

​	因此我们可以在Span结构中再增加一个_objSize成员，该成员代表着这个span管理的内存块别切成的一个个对象的大小。

```c++
//管理以页为单位的大块内存
struct Span {
	PAGE_ID _pageId = 0;        //大块内存起始页的页号
	size_t _n = 0;              //页的数量

	Span* _next = nullptr;      //双链表结构
	Span* _prev = nullptr;

	size_t _objSize = 0;        //切好的小对象的大小
	size_t _useCount = 0;       //切好的小块内存，被分配给thread cache的计数
	void* _freeList = nullptr;  //切好的小块内存的自由链表

	bool _isUse = false;        //是否在被使用
};
```

​	而所有的span都是从page cache中拿出来的，因此每当我们调用NewSpan获取到一个k页的span时，就应该将这个span的_objSize保存下来。

```c++
Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
span->_objSize = size;
```

​	代码只有两处，一处是在central cache中获取非空span时，如果central cache对应的桶中没有非空的span，此时会调用NewSpan获取一个k页的span；另一处是当申请大于256KB内存时，会直接调用NewSpan获取一个k页的span。

---

​	此时当我们释放对象时，就可以直接从对象的span中获取到该对象的大小，准确来说获取的是对齐以后的大小。

```c++
static void ConcurrentFree(void* ptr) {
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t size = span->_objSize;
	if (size > MAX_BYTES) { 
        //大于256KB的内存释放
		PageCache::GetInstance()->_pageMtx.lock();
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else {
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}
```

---

>读取映射关系时的加锁问题

​	我们将页号与span之间的映射关系是存储在PageCache类当中的，当我们访问这个映射关系时是需要加锁的，因为STL容器是不保证线程安全的。

​	对于当前代码来说，如果我们此时正在page cache进行相关操作，那么访问这个映射关系是安全的，因为当进入page cache之前是需要加锁的，因此可以保证此时只有一个线程在进行访问。

​	但如果我们是在central cache访问这个映射关系，或是在调用ConcurrentFree函数释放内存时访问这个映射关系，那么就存在线程安全的问题。因此当我们在page cache外部访问这个映射关系时是需要加锁的。

​	实际就是在调用page cache对外提供访问映射关系的函数时需要加锁，这里我们可以考虑使用C++当中的unique_lock，当然你也可以用普通的锁。

```c++
//获取从对象到span的映射
//获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj) {
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; //页号

	std::unique_lock<std::mutex> lock(_pageMtx); //构造时加锁，析构时自动解锁
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end()) {
		return ret->second;
	}
	else {
		assert(false);
		return nullptr;
	}
}
```

---

## 多线程环境下对比malloc测试

​	之前我们只是对代码进行了一些基础的单元测试，下面我们在多线程场景下对比malloc进行测试。

```c++
#include "ConcurrentAlloc.h"
#include "ObjectPool.h"
#include <chrono>

//ntimes：单轮次申请和释放内存的次数
//nworks：线程数
//rounds：轮次（跑多少轮）
void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds) {
    std::vector<std::thread> vthread(nworks);
    std::atomic<size_t> malloc_costtime(0);
    std::atomic<size_t> free_costtime(0);

    for (size_t k = 0; k < nworks; ++k) {
        vthread[k] = std::thread([&, k]() {
            std::vector<void*> v;
            v.reserve(ntimes);
            for (size_t j = 0; j < rounds; ++j) {
                auto begin1 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++) {
                    //v.push_back(malloc(16));
                    v.push_back(malloc((16 + i) % 8192 + 1));
                }
                auto end1 = std::chrono::high_resolution_clock::now();
                auto begin2 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++) {
                    free(v[i]);
                }
                auto end2 = std::chrono::high_resolution_clock::now();
                v.clear();
                malloc_costtime += std::chrono::duration_cast<std::chrono::milliseconds>(end1 - begin1).count();
                free_costtime += std::chrono::duration_cast<std::chrono::milliseconds>(end2 - begin2).count();
            }
        });
    }
    for (auto& t : vthread) {
        t.join();
    }
    printf("%zu个线程并发执行%zu轮次，每轮次malloc %zu次: 花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());
    printf("%zu个线程并发执行%zu轮次，每轮次free %zu次: 花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());
    printf("%zu个线程并发malloc&free %zu次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}

void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds) {
    std::vector<std::thread> vthread(nworks);
    std::atomic<size_t> malloc_costtime(0);
    std::atomic<size_t> free_costtime(0);
    for (size_t k = 0; k < nworks; ++k) {
        vthread[k] = std::thread([&]() {
            std::vector<void*> v;
            v.reserve(ntimes);
            for (size_t j = 0; j < rounds; ++j) {
                auto begin1 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++) {
                    //v.push_back(ConcurrentAlloc(16));
                    v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
                }
                auto end1 = std::chrono::high_resolution_clock::now();
                auto begin2 = std::chrono::high_resolution_clock::now();
                for (size_t i = 0; i < ntimes; i++){
                    ConcurrentFree(v[i]);
                }
                auto end2 = std::chrono::high_resolution_clock::now();
                v.clear();
                malloc_costtime += std::chrono::duration_cast<std::chrono::milliseconds>(end1 - begin1).count();
                free_costtime += std::chrono::duration_cast<std::chrono::milliseconds>(end2 - begin2).count();
            }
        });
    }
    for (auto& t : vthread) {
        t.join();
    }
    printf("%zu个线程并发执行%zu轮次，每轮次concurrent alloc %zu次: 花费：%zu ms\n",
        nworks, rounds, ntimes, malloc_costtime.load());
    printf("%zu个线程并发执行%zu轮次，每轮次concurrent dealloc %zu次: 花费：%zu ms\n",
        nworks, rounds, ntimes, free_costtime.load());
    printf("%zu个线程并发concurrent alloc&dealloc %zu次，总计花费：%zu ms\n",
        nworks, nworks * rounds * ntimes, malloc_costtime.load() + free_costtime.load());
}

int main() {
    size_t n = 10000;
    std::cout << "==========================================================" << std::endl;
    BenchmarkConcurrentMalloc(n, 4, 10);
    std::cout << std::endl << std::endl;
    BenchmarkMalloc(n, 4, 10);
    std::cout << "==========================================================" << std::endl;
    return 0;
}
```

​	其中测试函数各个参数的含义如下：

- ntimes：单轮次申请和释放内存的次数。
- nworks：线程数。
- rounds：轮次。

​	在测试函数中，我们通过clock函数分别获取到每轮次申请和释放所花费的时间，然后将其对应累计到malloc_cost_time和free_cost_time上，最后我们就得到了，nworks个线程跑rounds轮，每轮申请和释放ntimes次，这个过程申请所消耗的时间、释放所消耗的时间、申请和释放总共消耗的时间。

​	注意，我们创建线程时让线程指向的是lambda表达式，而我们这里在使用lambda表达式时，以值传递的方式捕获了变量k，以引用传递的方式捕捉了其他父作用域中的变量，因此我们可以将各个线程消耗的时间累加到一起。

​	我们将所有的线程申请内存消耗的时间都累加到malloc_cost_time上，将释放内存消耗的时间都累加到free_cost_time上，此时malloc_cost_time和free_cost_time可能被多个线程同时进行累加操作的，所以存在线程安全的问题。鉴于此，我们在定义这两个变量时使用了atomic类模板，这时对它们的操作就是原子操作了。

---

>固定大小内存的申请和释放

​	我们先来测试一下固定大小内存的申请和释放：

```c++
v.push_back(malloc(16));
v.push_back(ConcurrentAlloc(16));
```

​	此时4个线程执行10轮操作，每轮申请释放10000次，总共申请释放了40万次，运行后可以看到，malloc的效率还是更高。

![image-20250319234035915](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503192340039.png)

​	由于此时我们申请释放的都是固定大小的对象，每个线程申请释放时访问的都是各自的thread cache的同一个桶，当thread cache的这个桶中没有对象或者对象太多要归还时，也都会访问central cache的同一个桶。此时central cache中的桶锁就不起作用了，因为我们让central cache使用桶锁的目的就是为了让多个thread cache可以同时访问central cache的不同桶，而此时每个thread cache访问的却是central cache中的同一个桶。

---

>不同大小内存的申请和释放

​	下面我们再来测试一下不同大小内存的申请和释放

```c++
v.push_back(malloc((16 + i) % 8192 + 1));
v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
```

​	运行后可以看到，由于申请和释放内存的大小是不同的，此时central cache当中的桶锁就起作用了，ConcurrentAlloc的效率也有了较大增长，但相比malloc来说还是差一点点。

![image-20250319233945080](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503192339142.png)

## 性能瓶颈分析

​	经过前面的测试可以看到，我们的代码此时与malloc之间还是有差距的，此时我们就应该分析分析我们当前项目的瓶颈在哪里，但着不能简单的凭感觉，我们应该用性能分析的工具来进行分析。

---

>VS编译器下性能分析的操作步骤

​	VS编译器中就带有性能分析的工具的，我们可以依次点击“调试->性能和诊断” 进行性能分析，注意该操作要在Debug模式下进行。

![image-20250319234506992](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503192345380.png)

​	同时我们将代码中的n的值由10000调成了1000，否则该分析过程可能会花费较多的时间，并且将malloc的测试代码进行了屏蔽，因为我们要分析的是我们自己实现的高并发内存池。

```c++
int main() {
	size_t n = 1000;
	cout << "==========================================================" <<
		endl;
	BenchmarkConcurrentMalloc(n, 4, 10);
	cout << endl << endl;
	//BenchmarkMalloc(n, 4, 10);
	cout << "==========================================================" <<
		endl;
	return 0;
}
```

​	在点击了“调试->性能和诊断” 后会弹出一个提示框，我们直接点击“开始”进行了。
![image-20250319234906300](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503192349419.png)

---

> 分析性能瓶颈

​	通过分析结果可以看到，Deallocate和MapObjectToSpan这两个函数就占用了大量的时间
![image-20250320000432184](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503200004410.png)

​	而在Deallocate函数中，调用ListTooLong函数时消耗的时间是最多的。

![image-20250320000609627](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503200006714.png)

​	继续往下看，在ListTooLong函数中，调用ReleaseListToSpans函数时消耗的时间是最多的。再进一步看，在ReleaseListToSpans函数中，调用MapObjectToSpan函数时消耗的时间是最多的。

![image-20250320000751122](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503200007196.png)

​	也就是说，最终消耗时间最多的实际上就是MapObjectToSpan函数，我们这时再来看看为什么调用MapObjectToSpan函数会消耗这么多时间，根据观察，调用该函数时会消耗这么多时间就是因为锁的原因。

​	因为当前项目的瓶颈点就在锁竞争上面，需要解决调用MapObjectToSpan函数访问映射关系时的加锁问题。tcmalloc当中针对这一点使用了基数数进行优化，使得在读取这个映射关系时可以做到不加锁。

---

## 针对性能瓶颈使用基数树进行优化

​	基数树实际上就是一个分层的哈希表，根据所分层数不同可分成单层基数树、二层基数树、三层基数树等。
>单层基数树

​	单层基数树实际采用的就是直接定址法，每一个页号对应span的地址就存储数组中以该页号为下标的位置上。
![image-20250320001742802](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503200017870.png)
​	最坏的情况下我们需要建立所有页号与其span之间的映射关系，因此这个数组中元素个数应该与页号的数目相同，数组中每个位置存储的就是对应span的指针。

```c++
//单层基数树
template <int BITS>
class TCMalloc_PageMap1 {
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap1() {
		size_t size = sizeof(void*) << BITS; //需要开辟数组的大小
		size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT); //按页对齐后的大小
		array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT); //向堆申请空间
		memset(array_, 0, size); //对申请到的内存进行清理
	}
	void* get(Number k) const {
		if ((k >> BITS) > 0){ 
            //k的范围不在[0, 2^BITS-1]
			return NULL;
		}
		return array_[k]; //返回该页号对应的span
	}
	void set(Number k, void* v) {
		assert((k >> BITS) == 0); //k的范围必须在[0, 2^BITS-1]
		array_[k] = v; //建立映射
	}
private:
	void** array_; //存储映射关系的数组
	static const int LENGTH = 1 << BITS; //页的数目
};
```

​	此时当我们需要建立映射时就调用set函数，需要读取映射关系时，就调用get函数就行了。

​	代码中的非类型模板参数BITS表示存储页号最多需要比特位的个数。在32位下我们传入的是32-PAGE_SHIFT，在64位下传入的是64-PAGE_SHIFT。而其中的LENGTH成员代表的就是页号的数目，即2^BITS。

​	比如32位平台下，以一页大小为8K为例，此时页的数目为2^32 / 2^13 = 2^19，因此存储页号最多需要19个比特位，此时传入非类型模板参数的值就是32-13=19。由于32位平台下指针的大小是4字节，因此该数组的大小就是2 ^19 * 4 = 2 ^ 21 = 2M，内存消耗不大，是可行的。但是如果是在64位平台下，此时数组的大小是 2^51 * 8 = 2 ^ 54 = 2^24 G，这显然是不可行的，实际上对于64位的平台，我们需要使用三层基数树。

---

>二层基数树

​	这里还是以32位平台下，一页的大小为8K为例来说明，此时存储页号最多需要19个比特位。而二层基数树实际上就是把这19个比特位分为两次进行映射。

​	比如用前5个比特位在基数树的第一层进行映射，映射后得到对应的第二层，然后用剩下的比特位在基数树的第二层进行映射，映射后最终得到该页号对应的span指针。
![image-20250320003353387](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503200033558.png)

​	在二层基数树中，第一层的数组占用2^5 * 4 = 2^7 Byte空间，第二层的数组最多占用 2^5 * 2^14 *4 = 2 ^ 21 = 2 M。二层基数树相比一层基数树的好处就是，一层基数树必须一开始就把2M的数组开辟出来，而二层基数树一开始时只需要将第一层的数组开辟出来，当需要进行某一页号映射时再开辟对应的第二层的数组就行了。

```c++
//二层基数树
template <int BITS>
class TCMalloc_PageMap2 {
private:
	static const int ROOT_BITS = 5;                //第一层对应页号的前5个比特位
	static const int ROOT_LENGTH = 1 << ROOT_BITS; //第一层存储元素的个数
	static const int LEAF_BITS = BITS - ROOT_BITS; //第二层对应页号的其余比特位
	static const int LEAF_LENGTH = 1 << LEAF_BITS; //第二层存储元素的个数
	//第一层数组中存储的元素类型
	struct Leaf {
		void* values[LEAF_LENGTH];
	};
	Leaf* root_[ROOT_LENGTH]; //第一层数组
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap2() {
		memset(root_, 0, sizeof(root_)); //将第一层的空间进行清理
		PreallocateMoreMemory(); //直接将第二层全部开辟
	}
	void* get(Number k) const {
		const Number i1 = k >> LEAF_BITS;        //第一层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1); //第二层对应的下标
		if ((k >> BITS) > 0 || root_[i1] == NULL) { 
            //页号值不在范围或没有建立过映射
			return NULL;
		}
		return root_[i1]->values[i2]; //返回该页号对应span的指针
	}
	void set(Number k, void* v) {
		const Number i1 = k >> LEAF_BITS;        //第一层对应的下标
		const Number i2 = k & (LEAF_LENGTH - 1); //第二层对应的下标
		assert(i1 < ROOT_LENGTH);
		root_[i1]->values[i2] = v; //建立该页号与对应span的映射
	}
	//确保映射[start,start_n-1]页号的空间是开辟好了的
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> LEAF_BITS;
			if (i1 >= ROOT_LENGTH) //页号超出范围
				return false;
			if (root_[i1] == NULL){ 
                //第一层i1下标指向的空间未开辟
			
				//开辟对应空间
				static ObjectPool<Leaf> leafPool;
				Leaf* leaf = (Leaf*)leafPool.New();
				memset(leaf, 0, sizeof(*leaf));
				root_[i1] = leaf;
			}
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS; //继续后续检查
		}
		return true;
	}
	void PreallocateMoreMemory() {
		Ensure(0, 1 << BITS); //将第二层的空间全部开辟好
	}
};
```

​	因此在第二层基数树中有一个Ensure函数，当需要建立某一页号与其span之间的映射关系时，需要先调用该Ensure函数确保用于映射该页号的空间是开辟了的，如果没有开辟则会立即开辟。

​	而在32位平台下，就算第二层基数树第二层的数组全部开辟出来也就消耗了2M的空间，内存消耗也不算太多，因此我们可以在构造第二层基数树的时候就把第二层数组全部开辟出来。

---

>三层基数树

​	上面一层基数树和二层基数树都适用于32位平台，而对于64位平台就需要使用三层基数数了。三层基数树与二层基数树类似，三层基数数实际上就是把存储页号的若干比特位分为三次进行映射。

![image-20250320100121692](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503201001893.png)
	此时只有当要建立某一页号的映射关系时，再开辟对于的数组空间，而没有建立映射的页号就可以不用开辟其对应的数据空间 ，此时就能在一定程度上节省内存空间。

```c++
//三层基数树
template <int BITS>
class TCMalloc_PageMap3 {
private:
	static const int INTERIOR_BITS = (BITS + 2) / 3;       //第一、二层对应页号的比特位个数
	static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS; //第一、二层存储元素的个数
	static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS; //第三层对应页号的比特位个数
	static const int LEAF_LENGTH = 1 << LEAF_BITS;         //第三层存储元素的个数
	struct Node {
		Node* ptrs[INTERIOR_LENGTH];
	};
	struct Leaf {
		void* values[LEAF_LENGTH];
	};
	Node* NewNode() {
		static ObjectPool<Node> nodePool;
		Node* result = nodePool.New();
		if (result != NULL) {
			memset(result, 0, sizeof(*result));
		}
		return result;
	}
	Node* root_;
public:
	typedef uintptr_t Number;
	explicit TCMalloc_PageMap3() {
		root_ = NewNode();
	}
	void* get(Number k) const {
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);         //第一层对应的下标
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1); //第二层对应的下标
		const Number i3 = k & (LEAF_LENGTH - 1);                    //第三层对应的下标
		//页号超出范围，或映射该页号的空间未开辟
		if ((k >> BITS) > 0 || root_->ptrs[i1] == NULL || root_->ptrs[i1]->ptrs[i2] == NULL) {
			return NULL;
		}
		return reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3]; //返回该页号对应span的指针
	}
	void set(Number k, void* v) {
		assert(k >> BITS == 0);
		const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);         //第一层对应的下标
		const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1); //第二层对应的下标
		const Number i3 = k & (LEAF_LENGTH - 1);                    //第三层对应的下标
		Ensure(k, 1); //确保映射第k页页号的空间是开辟好了的
		reinterpret_cast<Leaf*>(root_->ptrs[i1]->ptrs[i2])->values[i3] = v; //建立该页号与对应span的映射
	}
	//确保映射[start,start+n-1]页号的空间是开辟好了的
	bool Ensure(Number start, size_t n) {
		for (Number key = start; key <= start + n - 1;) {
			const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);         //第一层对应的下标
			const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1); //第二层对应的下标
			if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH) //下标值超出范围
				return false;
			if (root_->ptrs[i1] == NULL) { 
                //第一层i1下标指向的空间未开辟
			
				//开辟对应空间
				Node* n = NewNode();
				if (n == NULL) return false;
				root_->ptrs[i1] = n;
			}
			if (root_->ptrs[i1]->ptrs[i2] == NULL) { 
                //第二层i2下标指向的空间未开辟
			
				//开辟对应空间
				static ObjectPool<Leaf> leafPool;
				Leaf* leaf = leafPool.New();
				if (leaf == NULL) return false;
				memset(leaf, 0, sizeof(*leaf));
				root_->ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
			}
			key = ((key >> LEAF_BITS) + 1) << LEAF_BITS; //继续后续检查
		}
		return true;
	}
	void PreallocateMoreMemory()
	{}
};
```

​	因此当我们要建立某一页的映射关系时，需要先确保存储该页映射的数组空间是开辟好了的，也就是调用代码中的Ensure函数，如果对应数组空间为开辟则会立马开辟对应的空间。

---

## 使用基数树进行优化代码实现

>代码更改

​	现在我们用基数树对代码进行优化，此时将PageCache类当中的unorder_map用基数树进行替换即可，由于当前是32位平台，因此这里随便用基层基数树都可以。

```c++
//单例模式
class PageCache {
public:
	//...
private:
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;
};
```

​	此时当我们需要建立页号与span的映射时，就调用基数树当中的set函数。

```c++
_idSpanMap.set(span->_pageId, span);
```

​	而当我们需要读取某一页对应的span时，就调用基数树当中的get函数。

```c++
Span* ret = (Span*)_idSpanMap.get(id);
```

​	并且现在是PageCache类 向外提供的，用于读取映射关系的MapObjectToSpan函数内部就不需要加锁了。

```c++
//获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj) {
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT; //页号
	Span* ret = (Span*)_idSpanMap.get(id);
	assert(ret != nullptr);
	return ret;
}
```

---

>为什么读取基数树映射关系时不需要加锁？

​	当某个线程在读取映射关系时，可能另一个线程正在建立其他页号的映射关系，而此时无论我们用的是C++当中的map还是unordered_map，在读取映射关系时都是需要加锁的。

​	因为C++中的map底层数据结构是红黑树，unordered_map的底层数据结构是哈希表，而无论是红黑树还是哈希表，当我们在插入数据时其底层的结构都有可能会发生变化。比如红黑树在插入数据时可能会引起树的旋转，而哈希表在插入数据时可能会引起哈希表扩容。此时要避免出现数据不一致的问题，就不能让插入操作和读取操作同时进行，因此我们在读取映射关系的时候是需要加锁的。

​	而对于基数树来说就不一样了，基数树的空间一旦开辟好了就不会发生变化，因此无论什么时候去读取某个页的映射，都是对应在一个固定的位置进行读取的。并且我们不会同时对同一个页进行读取映射和建立映射的操作，因为我们只有在释放对象时才需要读取映射，而建立映射的操作都是在page cache进行的。也就是说，读取映射时读取的都是对应span的_useCount不等于0的页【寻找，归还】，而建立映射时建立的都是对应span的 _useCount等于0的页，所以说我们不会同时对同一个页进行读取映射和建立映射的操作。

---

>再次对比malloc进行测试

​	还是同样的代码，只不过我们用基数数对代码进行了优化，此时测试固定大小内存的申请和释放的结果如下：

![image-20250320103816159](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503201038428.png)

​	可以看到，这时就算申请释放的是固定大小的对象，其效率都是malloc的两倍。下面在申请释放不同大小的对象时，由于central cache的桶锁起作用了，其效率更是变为了malloc的好几倍。

![image-20250320103726481](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503201037602.png)

---

>打包成动静态库

​	实际Google开源的的tcmalloc是会直接用于替换malloc的，不同平台替换的方式不同。比如基于Unix的系统上的glibc，使用了weak alias的方式替换；而对于某些其他平台，需要使用hook的钩子技术来做。

​	对于我们当前实现的项目，可以考虑将其打包成静态库或动态库。我们先右击点击解决资源管理器当中的项目名称，然后选择属性；此时会弹出该选项卡，按照以下图示就可以选择将其打包成静态库或动态库了。
![image-20250320105202917](https://gitee.com/li__jiahao/cloudimages/raw/master/img/202503201052260.png)

---

## 目前存在的未修复的Bug

- 无法在Linux下运行，存在未修复的段错误
- 64位基数树无法使用

## 项目源码

Github：https://github.com/Coder-li-jiahao/ConcurrentMemoryPool
