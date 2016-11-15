#ifndef _load_balancing_thread_pool_hpp_
#define _load_balancing_thread_pool_hpp_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <cmath>
#include <numeric>
#include <iostream>

template<typename T>
void sync_print(T const& value){
    static std::mutex pr;
    std::unique_lock<std::mutex> lk{pr};
    std::cout<<value<<std::endl;
}


template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
class basic_load_balancing_thread_pool
{
private:
    using work_signiture = void();
    using work_type = std::function<work_signiture>;
    using queue_type = queue_t<work_type,allocator_t<work_type>>;

    class worker
    {
    public:
        worker():_thread(std::bind(&worker::work,this)),_load(0),_running(true){}
        ~worker(){
            if(_thread.joinable()){
                _thread.join();
            }
        }
        bool push(work_type&& w){
            if(_running){
                std::unique_lock<std::mutex> lk{_lock};
                _work_q.push_back(std::forward<work_type&&>(w));
                ++_load;
                notify();
                return true;
            }else return false;
        }
        std::size_t load(){
            return _load;
        }
        const std::size_t load() const{
            return _load;
        }
        void notify(){
            _cv.notify_one();
        }
        bool running(){
            return _running;
        }
        void running(bool value){
            _running = value;
        }

    private:
        void work(){
            while (_running)
            {
                std::unique_lock<std::mutex> lk{_lock};
                _cv.wait(lk,[this](){
                    return !_running || _load;
                });
                while (_load)
                {
                    auto task = std::move(_work_q.front());
                    _work_q.pop_front();
                    task();
                    --_load;
                }
            }
            while (_load)
            {
                auto task = std::move(_work_q.front());
                _work_q.pop_front();
                task();
                --_load;
            }
        }
        std::thread _thread;
        std::mutex _lock;
        std::atomic_size_t _load;
        std::atomic_bool _running;
        queue_type _work_q;
        std::condition_variable _cv;
    };
public:
    basic_load_balancing_thread_pool(std::size_t N = std::thread::hardware_concurrency()) :_thread_data(N > 0 ? N : std::thread::hardware_concurrency())
    {
        for(auto&& th: _thread_data){
            th = std::unique_ptr<worker>(new worker());
        }
    }
    ~basic_load_balancing_thread_pool()
    {
        for (auto&& w: _thread_data)
        {
            w->running(false);
            w->notify();
        }
    }

    basic_load_balancing_thread_pool(basic_load_balancing_thread_pool&)=delete;
    basic_load_balancing_thread_pool(basic_load_balancing_thread_pool&&)=delete;
    basic_load_balancing_thread_pool& operator=(basic_load_balancing_thread_pool&)=delete;
    basic_load_balancing_thread_pool& operator=(basic_load_balancing_thread_pool&&)=delete;


    std::size_t size() const
    {
        return _thread_data.size();
    }

    void resize(std::size_t const& N)
    {
        long old = _thread_data.size();
        long diff = std::abs((double) N - old);
        if(N > old)
        {
            for(std::size_t i = 0; i < diff; ++i)
            {
                _thread_data.push_back(std::unique_ptr<worker>(new worker()));
            }
        }
        else if(old > N)
        {
            for(std::size_t i =0 ; i < diff; ++i)
            {
                _thread_data.back()->running(false);
                _thread_data.back()->notify();
                std::unique_ptr<worker> temp;
                std::swap(temp,_thread_data.back());
                _thread_data.pop_back();
            }
        }
    }



    template<typename func,typename... args_t>
    auto push(func && f, args_t&&... args) -> std::future<decltype(f(args...))>
    {
        auto&& pk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<func&&>(f),std::forward<args_t&&>(args)...));
        //wrap task
        worker* lowest = _thread_data.front().get();
        for(auto&& data: _thread_data)
        {
            if(data->running() && (data->load() < lowest->load()))
            {
                lowest = data.get();
            }
        }
        lowest->push([pk]()
        {
            (*pk)();
        });
        return pk->get_future();
    }
private:

    std::vector<std::unique_ptr<worker>> _thread_data;
};

using thread_pool = basic_load_balancing_thread_pool<std::deque,std::allocator>;
#endif
