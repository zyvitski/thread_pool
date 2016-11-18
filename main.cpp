#include "thread_pool.hpp"
#include <iostream>
#include <chrono>

template<typename T>
void sync_print(T const& value){
    static std::mutex pr;
    std::unique_lock<std::mutex> lk{pr};
    std::cout<<value<<std::endl;
}

int main(int argc, char** argv)
{
    try{
        thread_pool pool{4};
        std::vector<std::future<int>> out;
        auto it = std::chrono::high_resolution_clock::now();
        for(int i =0; i < 1000000;++i)
        {
            out.push_back(pool.push([i](){
                return i;
            }));
        }
        out.back().wait();
        auto ot = std::chrono::high_resolution_clock::now();
        std::cout<< std::chrono::duration<double>(ot - it).count()<<std::endl;
    }catch(std::exception& e){
        sync_print(e.what());
    }
    return 0;
}
