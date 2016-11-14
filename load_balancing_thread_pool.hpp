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


template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
class basic_load_balancing_thread_pool
{
private:
    using work_signiture = void();
    using work_type = std::function<work_signiture>;
    using queue_type = queue_t<work_type,allocator_t<work_type>>;

    class worker{
    public:
        worker():_thread(std::bind(&worker::work,this)),_load(0),_running(true){}
        ~worker(){
            if(_thread.joinable()){
                _thread.join();
            }
        }
        void push(work_type&& w){
            _work.push_back(std::forward<work_type&&>(w));
            std::atomic_thread_fence(std::memory_order_release);
            std::atomic_fetch_add_explicit(&_load,1ul,std::memory_order_relaxed);
            notify();
        }
        std::size_t load(){

            return std::atomic_load_explicit(&_load,std::memory_order_acquire);
        }
        const std::size_t load() const{
            return std::atomic_load_explicit(&_load,std::memory_order_acquire);
        }
        void notify(){
            _cv.notify_one();
        }
        bool running(){
            return std::atomic_load_explicit(&_running,std::memory_order_acquire);
        }
        void running(bool value){
            std::atomic_store_explicit(&_running,value,std::memory_order_release);
        }

    private:
        void work(){
            {
                std::unique_lock<std::mutex> lk{_lock};
                _cv.wait(lk,[this](){
                    return std::atomic_load_explicit(&_load,std::memory_order_acquire) || !running();
                });
            }

            while (running())
            {
                while (std::atomic_load_explicit(&_load,std::memory_order_acquire))
                {
                    consume();
                }
            }
            while (std::atomic_load_explicit(&_load,std::memory_order_acquire))
            {
                consume();
            }
        }
        inline void consume(){
            if(std::atomic_load_explicit(&_load,std::memory_order_relaxed)){
                work_type task = std::move(_work.front());
                _work.pop_front();
                task();

                std::atomic_thread_fence(std::memory_order_acquire);
                std::atomic_thread_fence(std::memory_order_release);

                std::atomic_fetch_sub_explicit(&_load,1ul,std::memory_order_relaxed);
            }
        }

        std::thread _thread;
        std::mutex _lock;
        std::atomic_size_t _load;
        std::atomic_bool _running;
        queue_type _work;
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
