#include "load_balancing_thread_pool.hpp"
#include "type_erased_task.hpp"
#include <iostream>



int main(int argc, char** argv)
{
    try{
        thread_pool pool{2};
        std::vector<std::future<int>> out;
        for(int i =0; i < 100;++i){
            out.push_back(pool.push([i](){
                return i;
            }));
        }
        for(auto&& o: out){
            std::cout<<o.get()<<std::endl;
        }
    }catch(std::exception& e){
        sync_print(e.what());
    }
    return 0;
}
