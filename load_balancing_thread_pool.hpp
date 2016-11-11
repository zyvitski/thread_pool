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


template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
class basic_load_balancing_thread_pool
{
private:
    using work_signiture = void();
    using work_type = std::function<work_signiture>;
    struct thread_data
    {
        std::thread _thread;
        std::mutex _lock;
        std::atomic<std::size_t> _load;
        std::atomic_bool _quit;
        queue_t<work_type> _work;
    };
public:

    basic_load_balancing_thread_pool(std::size_t N = std::thread::hardware_concurrency()) :_thread_data(N)
    {
        for(auto&& th: _thread_data){
            th = std::unique_ptr<thread_data>(new thread_data());
            th->_thread = std::thread(init_thread(*th));
        }
    }
    ~basic_load_balancing_thread_pool()
    {
        for (auto&& w: _thread_data)
        {
            w->_quit = true;
        }
        _cv.notify_all();
        for(auto&& w : _thread_data)
        {
            if (w->_thread.joinable())
            {
                w->_thread.join();
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
        diff = std::abs((double)diff);
        if(N > old)
        {
            for(std::size_t i =0; i < diff; ++i)
            {
                _thread_data.push_back(std::unique_ptr<thread_data>(new thread_data()));
                _thread_data.back()->_thread=std::thread(init_thread(*_thread_data.back()));
            }
        }
        else if(old > N)
        {
            for(std::size_t i=0; i <diff; ++i)
            {
                _thread_data.back()->_quit = true;
                if(_thread_data.back()->_thread.joinable())
                {
                    _thread_data.back()->_thread.join();
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
        _push([pk](){
            (*pk)();
        });
        _cv.notify_all();
        return pk->get_future();
    }

private:
    template<typename T>
    void _push(T&& value)
    {
        std::size_t avg_load = average_load();
        bool done=false;
        for(auto&& data: _thread_data){
            if((data->_load < avg_load) && !data->_quit){
                std::unique_lock<std::mutex> lk{data->_lock};
                data->_work.push_back(std::forward<T&&>(value));
                ++data->_load;
                done = true;
                break;
            }
        }
        //this branch will default to begin if the system is empty
        if (!done)
        {
            using iterator = typename decltype(_thread_data)::iterator;
            iterator lowest_index = _thread_data.begin();
            std::size_t lowest = (*lowest_index)->_load;
            for(iterator data = _thread_data.begin(); data != _thread_data.end(); ++data){
                if ((*data)->_load < lowest && !(*data)->_quit)
                {
                    lowest_index = data;
                    lowest = (*data)->_work.size();
                }
            }
            std::unique_lock<std::mutex> lk{(*lowest_index)->_lock};
            (*lowest_index)->_work.push_back(std::forward<T&&>(value));
            ++(*lowest_index)->_load;
        }
    }

    std::size_t average_load()
    {
        if (_thread_data.size())
        {
            double avg=0;
            for(auto&& i: _thread_data){
                avg += i->_load;
            }
            avg/= _thread_data.size();
            return std::ceil(avg);
        }
        else
        {
            return {};
        }
    }
    work_type init_thread(thread_data& _data)
    {
        //where index is the index used to match up the threads to the queues and loads
        return [&](){
            thread_data& data = const_cast<thread_data&>(_data);
            auto&& consume = [&](){
                work_type task;
                {
                    std::unique_lock<std::mutex> lk(data._lock);
                    task = std::move(data._work.front());
                    data._work.pop_front();
                }
                task();
                --data._load;
            };
            while (!data._quit)
            {
                std::unique_lock<std::mutex> lk{_sleep};
                _cv.wait(lk,[&](){
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
    std::condition_variable _cv;
    std::mutex _sleep;
};

using thread_pool = basic_load_balancing_thread_pool<std::deque,std::allocator>;
