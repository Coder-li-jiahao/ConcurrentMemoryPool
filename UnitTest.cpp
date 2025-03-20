#include"ConcurrentAlloc.h"

void Alloc1(){

    // //找page cache申请
    // void* p1 = ConcurrentAlloc(257 * 1024); //257KB
    // ConcurrentFree(p1, 257 * 1024);

    //找堆申请
    void* p2 = ConcurrentAlloc(129 * 8 * 1024); //129页
    ConcurrentFree(p2);


    // for (size_t i = 0; i < 1024; i++) {
	//     void* p1 = ConcurrentAlloc(6);
    // }
    
    // void* p1 = ConcurrentAlloc(6);
    // void* p2 = ConcurrentAlloc(8);
    // void* p3 = ConcurrentAlloc(1);

    // ConcurrentFree(p1, 6);
    // ConcurrentFree(p2, 8);
    // ConcurrentFree(p3, 1);

    // void* p1 = ConcurrentAlloc(6);
    // void* p2 = ConcurrentAlloc(8);
    // void* p3 = ConcurrentAlloc(1);
    // for(size_t i=0;i<5;i++){
    //     void* ptr=ConcurrentAlloc(6);
    // }
}

// void Alloc2(){
//     for(size_t i=0;i<5;i++){
//         void* ptr=ConcurrentAlloc(7);
//     }
// }

void TLSTest(){
    std::thread t1(Alloc1);
    
    
	//std::thread t2(Alloc2);
	
    t1.join();
    //usleep(1000);
    //t2.join();

}


int main(){

    TLSTest();
    
    return 0;
}
