#include "load_balancing_thread_pool.hpp"
#include <iostream>



int main(int argc, char** argv)
{
    try{
        thread_pool pool;
        std::vector<std::future<int>> out;
        for(int i =0; i < 10000;++i){
            out.push_back(pool.push([](int k){
                //sync_print(k);
                ++k;
                return k;
            },i));
        }
        for(auto&& o: out){
            std::cout<<o.get()<<std::endl;
        }
    }catch(std::exception& e){
        sync_print(e.what());
    }
    return 0;
}
