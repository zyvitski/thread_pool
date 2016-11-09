#ifndef __load_balancing_thread_pool_hpp__
#define __load_balancing_thread_pool_hpp__
#endif

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <numeric>
#include <cmath>
#include <iostream>

/*
 design:
 1 queue per thread, all single producer single consumer
 N worker threads
 1 conductor in calling thread that will manage who gets handed work

 push(function,data){
 bool pushed = false;
 for each thread{
 if N jobs pending < average load as integer
 give work to that thread
 pushed = true;
 }
 if(not pushed){
 figure out who has least work pending
 give to that thread
 }
 }
 }
 }

 */

template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
class basic_load_balancing_thread_pool{
private:
    using work_signiture = void();
    using work_type = std::function<work_signiture>;
    struct thread_data
    {
        thread_data():_lock(new std::mutex()),_load(new std::atomic<std::size_t>(0)),_quit(new std::atomic_bool(false)){

        }
        thread_data(thread_data&& other):_thread(std::move(other._thread)),
                                         _lock(std::move(other._lock)),
                                         _load(std::move(other._load)),
                                         _quit(std::move(other._quit)),
                                         _work(std::move(other._work)){}
        thread_data& operator=(thread_data&& other){
            if (this != &other)
            {
                _thread = std::move(other._thread);
                _lock = std::move(other._lock);
                _load = std::move(other._load);
                _work = std::move(other._work);
                _quit = std::move(other._quit);
            }
            return *this;
        }
        std::thread _thread;
        std::unique_ptr<std::mutex> _lock;
        std::unique_ptr<std::atomic<std::size_t>> _load;
        std::unique_ptr<std::atomic_bool> _quit;
        queue_t<work_type> _work;
    };

public:

    basic_load_balancing_thread_pool(std::size_t N = std::thread::hardware_concurrency()) :_running(true),_forced_idle(false)
    {
        _thread_data.resize(N);
        for(std::size_t i = 0; i < N ; ++i)
        {
            _thread_data[i]._thread = std::thread(init_thread(i));
        }
    }
    ~basic_load_balancing_thread_pool()
    {
        _running = false;
        _cv.notify_all();
        for(auto&& w : _thread_data)
        {
            if (w._thread.joinable())
            {
                w._thread.join();
            }
        }
    }
    std::size_t size() const
    {
        return _thread_data.size();
    }

    void resize(std::size_t const& N)
    {
        long old = _thread_data.size();
        long diff = N - old;
        if(N > old)
        {
            for(std::size_t i =0; i < std::abs(diff); ++i)
            {
                _thread_data.push_back(thread_data{});
                _thread_data.back()._thread=std::thread(init_thread(old-1+i));
            }
        }
        else if(old > N)
        {

            for(std::size_t i=0; i < std::abs(diff); ++i)
            {
                *_thread_data.back()._quit = true;
                if(_thread_data.back()._thread.joinable()){
                    _thread_data.back()._thread.join();
                }   
            }

        }

    }

    basic_load_balancing_thread_pool(basic_load_balancing_thread_pool&)=delete;
    basic_load_balancing_thread_pool(basic_load_balancing_thread_pool&&)=delete;
    basic_load_balancing_thread_pool& operator=(basic_load_balancing_thread_pool&)=delete;
    basic_load_balancing_thread_pool& operator=(basic_load_balancing_thread_pool&&)=delete;


    template<typename func,typename... args_t>
    auto push(func && f, args_t&&... args) -> std::future<decltype(f(args...))>
    {
        auto&& pk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<func&&>(f),std::forward<args_t&&>(args)...));
        //wrap task
        {
            _push([pk](){
                (*pk)();
            });
            _cv.notify_all();
        }
        return pk->get_future();
    }

private:
    template<typename T>
    void _push(T&& value)
    {
        std::size_t avg_load = average_load();
        bool done=false;
        for(std::size_t i =0; i <_thread_data.size();++i)
        {
            if(*_thread_data[i]._load < avg_load)
            {
                std::unique_lock<std::mutex> lk{*_thread_data[i]._lock};
                _thread_data[i]._work.push_back(std::forward<T&&>(value));
                ++(*_thread_data[i]._load);
                done = true;
                break;
            }
        }
        //this branch will default to 0 if the system is empty
        if (!done)
        {
            std::size_t lowest_index=0;
            std::size_t lowest = *_thread_data[0]._load;
            for(std::size_t i = lowest_index;i<_thread_data.size();++i)
            {
                if(*_thread_data[i]._load < lowest)
                {
                    lowest_index = i;
                    lowest = _thread_data[i]._work.size();
                }
            }
            {
                std::unique_lock<std::mutex> lk{*_thread_data[lowest_index]._lock};
                _thread_data[lowest_index]._work.push_back(std::forward<T&&>(value));
                ++(*_thread_data[lowest_index]._load);
            }
        }
    }

    std::size_t average_load()
    {
        if (_thread_data.size())
        {
            double avg=0;
            for(auto&& i: _thread_data){
                avg += *i._load;
            }
            avg/= _thread_data.size();
            return std::ceil(avg);
            //average load rounded up
        }
        else
        {
            return {};
        }
    }
    work_type init_thread(std::size_t index)
    {
        //where index is the index used to match up the threads to the queues and loads
        return [this,index](){
            queue_t<work_type>& q = _thread_data[index]._work;
            std::atomic<std::size_t>& load = *_thread_data[index]._load;
            std::mutex& lock = *_thread_data[index]._lock;
            std::atomic_bool& quit = *_thread_data[index]._quit;
            while (_running)
            {
                std::unique_lock<std::mutex> lk{_sleep};
                _cv.wait(lk,[&](){
                    return load || !_running || _forced_idle;
                });
                if (!_forced_idle)
                {
                    while (load && !_forced_idle)
                    {
                        work_type task;
                        {
                            std::unique_lock<std::mutex> lk(lock);
                            task = std::move(q.front());
                            q.pop_front();
                        }
                        task();
                        --load;
                    }
                    if (!_running || quit)
                    {
                        break;
                    }
                }
            }
        };
    }
    std::vector<thread_data> _thread_data;
    std::atomic_bool _running;
    std::atomic_bool _forced_idle;
    std::condition_variable _cv;
    std::mutex _sleep;
};

using thread_pool = basic_load_balancing_thread_pool<std::deque,std::allocator>;
