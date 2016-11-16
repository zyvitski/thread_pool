#include "load_balancing_thread_pool.hpp"
#include <iostream>
#include <ctime>

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
        clock_t it = clock();
        for(int i =0; i < 100000;++i){
            out.push_back(pool.push([i](){
                return i;
            }));
        }
        out.back().wait();
        clock_t ot = clock();
        std::cout<< double(ot - it) / CLOCKS_PER_SEC<<std::endl;
    }catch(std::exception& e){
        sync_print(e.what());
    }
    return 0;
}
