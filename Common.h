#pragma once
#include <iostream>
#include <stdexcept>
#include <cstddef>
#include<vector>
#include<assert.h>

#ifdef _WIN32
#include<Windows.h>
#else
//linux下
#include <sys/mman.h>
#include <unistd.h>
#endif

//#include"ObjectPool.h"

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