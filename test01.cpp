#include <iostream>
#include <vector>
#include <chrono>

#include "ThreadPool.h"

int AddSum(int a, int b){
    std::cout << "hello " << a+b << std::endl;
    return a+b;
}

void AddTimeWork(int a, int b){

    auto time_now  = std::chrono::system_clock::now();

    std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds> time_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(time_now);
	std::cout << "AddTimeWork: " << a + b << "---"<< time_ms.time_since_epoch().count() << std::endl;

}

int main()
{
    ThreadPool pool(1);
    std::vector< std::future<int> > results;
    /*
    for(int i = 0; i < 8; ++i) {
        results.emplace_back(
            pool.AddTask( [i] {
                std::cout << "hello " << i << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(1));
                //std::cout << "world " << i << std::endl;
                return i*i;
            })
        );
    }
    std::cout << "===003===  " << std::endl;
    results.emplace_back(pool.AddTask(AddSum, 100, 102));
    */

    pool.AddTimer(false, 5000, AddTimeWork, 3, 2);
    pool.AddTimer(false, 3000, AddTimeWork, 2, 1);
    pool.AddTimer(false, 1000, AddTimeWork, 1, 0);
    //for(auto && result: results)
    //    result.get();
    while(1){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    //std::cout << "===005===  " << std::endl;
    return 0;
}
