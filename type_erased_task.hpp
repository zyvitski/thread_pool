#ifndef type_erased_task_hpp
#define type_erased_task_hpp

#include <functional>
#include <future>
#include <exception>
#include <iostream>

template<typename func,typename... args_t>
auto type_erased_task(func&& f, args_t&&... args)->std::pair<std::function<void()>,std::future<decltype(f(args...))>>
{
    auto&& pk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<func&&>(f),std::forward<args_t&&>(args)...));
    return std::make_pair([pk](){
        try{
            (*pk)();
        }catch(std::exception& e){
            std::cerr<<e.what()<<std::endl;
        }
    },pk->get_future());
}

#endif
