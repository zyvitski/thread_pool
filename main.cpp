#include "load_balancing_thread_pool.hpp"
#include <iostream>

std::mutex pr;
int counter = 0;

template<typename T>
void sync_print(T const& value){
    std::unique_lock<std::mutex> lk{pr};
    std::cout<<counter++<<" "<< value<<std::endl;
}

int main(int argc, char** argv)
{
    try{
        thread_pool pool;

        for(int i =0; i < 10000;++i){
            pool.push([](int k){
                sync_print(k);
            },i);
        }
    }catch(std::exception& e){
        std::cout<<e.what()<<std::endl;
    }
    return 0;
}
