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



// //基准测试

// #include "ConcurrentAlloc.h"
// #include "ObjectPool.h"

// //ntimes：单轮次申请和释放内存的次数
// //nworks：线程数
// //rounds：轮次（跑多少轮）
// void BenchmarkMalloc(size_t ntimes, size_t nworks, size_t rounds) {
// 	std::vector<std::thread> vthread(nworks);
// 	std::atomic<size_t> malloc_costtime(0);
// 	std::atomic<size_t> free_costtime(0);
// 	for (size_t k = 0; k < nworks; ++k) {
// 		vthread[k] = std::thread([&, k]() {
// 			std::vector<void*> v;
// 			v.reserve(ntimes);
// 			for (size_t j = 0; j < rounds; ++j) {
// 				size_t begin1 = clock();
// 				for (size_t i = 0; i < ntimes; i++){
// 					//v.push_back(malloc(16));
// 					v.push_back(malloc((16 + i) % 8192 + 1));
// 				}
// 				size_t end1 = clock();
// 				size_t begin2 = clock();
// 				for (size_t i = 0; i < ntimes; i++) {
// 					free(v[i]);
// 				}
// 				size_t end2 = clock();
// 				v.clear();
// 				malloc_costtime += (end1 - begin1);
// 				free_costtime += (end2 - begin2);
// 			}
// 		});
// 	}
// 	for (auto& t : vthread) {
// 		t.join();
// 	}
// 	printf("%u个线程并发执行%u轮次，每轮次malloc %u次: 花费：%u ms\n",
// 		nworks, rounds, ntimes, malloc_costtime.load());
// 	printf("%u个线程并发执行%u轮次，每轮次free %u次: 花费：%u ms\n",
// 		nworks, rounds, ntimes, free_costtime.load());
// 	printf("%u个线程并发malloc&free %u次，总计花费：%u ms\n",
// 		nworks, nworks*rounds*ntimes, malloc_costtime.load() + free_costtime.load());
// }

// void BenchmarkConcurrentMalloc(size_t ntimes, size_t nworks, size_t rounds) {
// 	std::vector<std::thread> vthread(nworks);
// 	std::atomic<size_t> malloc_costtime(0);
// 	std::atomic<size_t> free_costtime(0);
// 	for (size_t k = 0; k < nworks; ++k) {
// 		vthread[k] = std::thread([&]() {
// 			std::vector<void*> v;
// 			v.reserve(ntimes);
// 			for (size_t j = 0; j < rounds; ++j) {
// 				size_t begin1 = clock();
// 				for (size_t i = 0; i < ntimes; i++) {
// 					//v.push_back(ConcurrentAlloc(16));
// 					v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));
// 				}
// 				size_t end1 = clock();
// 				size_t begin2 = clock();
// 				for (size_t i = 0; i < ntimes; i++){
// 					ConcurrentFree(v[i]);
// 				}
// 				size_t end2 = clock();
// 				v.clear();
// 				malloc_costtime += (end1 - begin1);
// 				free_costtime += (end2 - begin2);
// 			}
// 		});
// 	}
// 	for (auto& t : vthread) {
// 		t.join();
// 	}
// 	printf("%u个线程并发执行%u轮次，每轮次concurrent alloc %u次: 花费：%u ms\n",
// 		nworks, rounds, ntimes, malloc_costtime.load());
// 	printf("%u个线程并发执行%u轮次，每轮次concurrent dealloc %u次: 花费：%u ms\n",
// 		nworks, rounds, ntimes, free_costtime.load());
// 	printf("%u个线程并发concurrent alloc&dealloc %u次，总计花费：%u ms\n",
// 		nworks, nworks*rounds*ntimes, malloc_costtime.load() + free_costtime.load());
// }

// int main() {
// 	size_t n = 10000;
// 	cout << "==========================================================" <<
// 		endl;
// 	BenchmarkConcurrentMalloc(n, 4, 10);
// 	cout << endl << endl;
// 	BenchmarkMalloc(n, 4, 10);
// 	cout << "==========================================================" <<
// 		endl;
// 	return 0;
// }