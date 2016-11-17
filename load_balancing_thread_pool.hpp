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
<<<<<<< HEAD
=======
#include <iostream>
#include "type_erased_task.hpp"
>>>>>>> dev


namespace workers{
    template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
    class worker
    {
    public:
        using work_signiture = void();
        using work_type = std::function<work_signiture>;
        using queue_type = queue_t<work_type,allocator_t<work_type>>;
        worker():_thread(std::bind(&worker::work,this)),_load(0),_running(true){}
        ~worker()
        {
            if(_thread.joinable())
            {
                _thread.join();
            }
        }
        bool push(work_type&& w)
        {
            if(_running)
            {
                std::unique_lock<std::mutex> lk{_lock};
                _work_q.push_back(std::forward<work_type&&>(w));
                ++_load;
                _cv.notify_one();
                return true;
            }else return false;
        }
        bool push(queue_type&& w)
        {
            if(_running){
                std::unique_lock<std::mutex> lk{_lock};
                while (w.size())
                {
                    _work_q.push_back(std::move(w.front()));
                    w.pop_front();
                    ++_load;
                }
                _cv.notify_one();
                return true;
            }else return false;
        }
        std::size_t load()
        {
            return _load;
        }
        const std::size_t load() const
        {
            return _load;
        }
        bool running()
        {
            return _running;
        }
        void running(bool value)
        {
            _running = value;
            _cv.notify_one();
        }
        void notify()
        {
            _cv.notify_one();
        }
    private:
        void work(){
            while (_running)
            {
                {
                    std::unique_lock<std::mutex> lk{_lock};
                    _cv.wait(lk,[this](){
                        return !_running || _load;
                    });
                    std::swap(_work_q,_worker_copy);
                }
                while (!_worker_copy.empty())
                {
                    consume_one();
                }
            }
            while (!_worker_copy.empty())
            {
                consume_one();
            }
        }
        inline void consume_one()
        {
            auto task = std::move(_worker_copy.front());
            _worker_copy.pop_front();
            task();
            --_load;
        }
        std::thread _thread;
        std::mutex _lock;
<<<<<<< HEAD
        std::atomic<std::size_t> _load;
        std::atomic_bool _quit;
        queue_t<work_type> _work;
=======
        std::atomic_size_t _load;
        std::atomic_bool _running;
        queue_type _work_q;
        queue_type _worker_copy;
>>>>>>> dev
        std::condition_variable _cv;
    };

    template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
    using default_worker = worker<queue_t,allocator_t>;

}

template< typename worker_t>
class basic_load_balancing_thread_pool
{
public:
    using worker_type = worker_t;
    using work_signiture = void();
    using work_type = std::function<work_signiture>;
    using queue_type = typename worker_type::queue_type;
    basic_load_balancing_thread_pool(std::size_t N = std::thread::hardware_concurrency()) :_thread_data(N > 0 ? N : std::thread::hardware_concurrency())
    {
        for(auto&& th: _thread_data){
#ifdef __cpp_lib_make_unique
            th = std::make_unique<work_type>();
#else
            th = std::unique_ptr<worker_type>(new worker_type());
#endif
        }
    }
    ~basic_load_balancing_thread_pool()
    {
        for (auto&& w: _thread_data)
        {
<<<<<<< HEAD
            w->_quit = true;
            w->_cv.notify_one();
        }
        for(auto&& w : _thread_data)
        {
            if (w->_thread.joinable())
            {
                w->_thread.join();
            }
=======
            w->running(false);
            w->notify();
>>>>>>> dev
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
<<<<<<< HEAD
        long diff = std::abs((double)N - old);
=======
        long diff = std::abs((double) N - old);
>>>>>>> dev
        if(N > old)
        {
            for(std::size_t i = 0; i < diff; ++i)
            {
#ifdef __cpp_lib_make_unique
                _thread_data.push_back(std::make_unique<worker_type>());
#else
                _thread_data.push_back(std::unique_ptr<worker_type>(new worker_type()));
#endif
            }
        }
        else if(old > N)
        {
            for(std::size_t i =0 ; i < diff; ++i)
            {
                _thread_data.back()->running(false);
                _thread_data.back()->notify();
                std::unique_ptr<worker_type> temp;
                std::swap(temp,_thread_data.back());
                _thread_data.pop_back();
            }
        }
    }
    void push(queue_type&& q)
    {
        _decide_push()->push(std::forward<queue_type&&>(q));
    }

    template<typename func,typename... args_t>
    auto push(func && f, args_t&&... args) -> std::future<decltype(f(args...))>
    {
<<<<<<< HEAD
        auto&& pk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<func&&>(f),std::forward<args_t&&>(args)...));
        //wrap task
        _balanced_push([pk]()
        {
            (*pk)();
        });
        //_cv.notify_all();
        return pk->get_future();
    }
    std::size_t average_load() const
    {
        if (_thread_data.size())
        {
            auto&& summer = [](double const& lhs, std::unique_ptr<thread_data> const& rhs){
                return lhs + rhs->_load;
            };
            return std::ceil(std::accumulate(_thread_data.begin(),_thread_data.end(),0,summer)/_thread_data.size());
        }
        else
        {
            return {};
        }
=======
        auto task = type_erased_task(std::forward<func&&>(f),std::forward<args_t&&>(args)...);
        _decide_push()->push(std::move(task.first));
        return std::move(task.second);
>>>>>>> dev
    }
private:
    worker_type* _decide_push(){
        worker_type* lowest = _thread_data.front().get();
        for(auto&& data: _thread_data)
        {
<<<<<<< HEAD
            if(!data->_quit)
            {
                if((data->_load < avg_load))
                {
                    std::lock_guard<std::mutex> lk{data->_lock};
                    data->_work.push_back(std::forward<T&&>(value));
                    ++data->_load;
                    data->_cv.notify_one();
                    done = true;
                    break;
                }
            }
        }
        //this branch will default to begin if the system is empty
        if (!done)
        {
            using iterator = typename decltype(_thread_data)::iterator;
            iterator lowest_index = _thread_data.begin();
            for(iterator data = _thread_data.begin(); data != _thread_data.end(); ++data)
            {
                if(!(*data)->_quit)
                {
                    if ((*data)->_load < (*lowest_index)->_load)
                    {
                        lowest_index = data;
                    }
                }
            }
            std::lock_guard<std::mutex> lk{(*lowest_index)->_lock};
            (*lowest_index)->_work.push_back(std::forward<T&&>(value));
            ++(*lowest_index)->_load;
            (*lowest_index)->_cv.notify_one();
        }
    }



    work_type init_thread(thread_data& data)
    {
        //where index is the index used to match up the threads to the queues and loads
        return [&]()
        {
            auto&& consume = [&]()
            {
                work_type task;
                {
                    std::unique_lock<std::mutex> lk{data._lock};
                    task = std::move(data._work.front());
                    data._work.pop_front();
                }
                task();
                --data._load;
            };
            while (!data._quit)
            {
                std::unique_lock<std::mutex> lk{_sleep};
                data._cv.wait(lk,[&](){
                    return data._load || data._quit;
                });
                while (data._load)
                {
                    consume();
                }
            }
            while (data._load)
            {
                consume();
            }
        };
    }

    std::vector<std::unique_ptr<thread_data>> _thread_data;
    std::mutex _sleep;
=======
            if(data->running() && (data->load() < lowest->load()))
            {
                lowest = data.get();
            }
        }
        return lowest;
    }

    std::vector<std::unique_ptr<worker_type>> _thread_data;
>>>>>>> dev
};

using thread_pool = basic_load_balancing_thread_pool<workers::default_worker<std::deque,std::allocator>>;
#endif
