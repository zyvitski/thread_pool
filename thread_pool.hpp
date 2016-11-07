

#ifndef thread_pool_h
#define thread_pool_h

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

template< template<typename...> class queue_t, template<typename...> class allocator_t = std::allocator>
class basic_thread_pool
{
    using function_wrapper_type = std::function<void()>;
    using queue_type = queue_t<function_wrapper_type,allocator_t<function_wrapper_type>>;
public:

    using size_type = std::size_t;


    explicit basic_thread_pool(size_type threads = std::thread::hardware_concurrency(),
                               bool should_finish_work=true) :_workers(threads),
                                                              _should_finish_before_exit(should_finish_work),
                                                              _running(true),
                                                              _forced_idle(false)
    {
        for(auto&& thrd: _workers){
            thrd = std::thread(thread_init());
        }
    }

    ~basic_thread_pool()
    {
        //clear all work if we dont need to do it before exit
        if (!_should_finish_before_exit) {
            clear();
        }

        //wake all threads
        _running = false;
        _cv.notify_all();

        //join all threads
        for (auto&& thrd: _workers) {
            if(thrd.joinable()){
                thrd.join();
            }
        }
    }

    //non moveable
    basic_thread_pool(basic_thread_pool&&) = delete;
    basic_thread_pool& operator=(basic_thread_pool&&)= delete;

    //non copyable
    basic_thread_pool(basic_thread_pool&)=delete;
    basic_thread_pool& operator=(basic_thread_pool&)=delete;

    //number of workers
    const size_type size()
    {
        std::unique_lock<std::mutex> lk{_workers_mutex};
        return _workers.size();
    }

    //resize number of workers
    void resize(size_type const& new_size)
    {
        //lock access to work and workers
        std::unique_lock<std::mutex> lk{_workers_mutex}, lk2{_work_mutex};
        auto old = _workers.size();
        if (new_size > old) {
            //grow
            for(int i=0; i < (new_size-old);++i){
                _workers.emplace_back(thread_init());
            }
        }else if(new_size < old){
            _forced_idle=true;
            _cv.notify_all();//force idle
            for (int i=0; i<(old - new_size); ++i)
            {   _workers.back().detach();//break it away
                _workers.pop_back();//get rin of it
            }
            _forced_idle=false;
            _cv.notify_all();//end idle
        }
        //else no change needed

    }

    //clear all work
    void clear()
    {
        std::unique_lock<std::mutex> lk{_work_mutex};
        _work.clear();
    }


    //push a function onto the pool
    template<typename func,typename... args_t>
    auto push(func && f, args_t&&... args) -> std::future<decltype(f(args...))>
    {
        auto&& pk = std::make_shared<std::packaged_task<decltype(f(args...))()>>(std::bind(std::forward<func&&>(f),std::forward<args_t&&>(args)...));
        //wrap task
        {
            std::unique_lock<std::mutex> lk{_work_mutex};
            _work.push_back([pk](){
                (*pk)();
            });
        }
        _cv.notify_one();

        return pk->get_future();
    }



    //find out if the pool is set to finish work before exit
    bool will_finish_work_before_exit() const{
        return _should_finish_before_exit;
    }
    //modify _should_finish_befoer_exit
    void will_finish_work_before_exit(bool const& value){
        _should_finish_before_exit = value;
    }

protected:

    std::vector<std::thread> _workers;

    std::condition_variable _cv;

    std::mutex _work_mutex;

    std::mutex _workers_mutex;

    queue_type _work;

    std::atomic_bool _should_finish_before_exit;//do we need to finish all work before workers exit
    std::atomic_bool _running;//is the thread pool active
    std::atomic_bool _forced_idle;//should all threads run as idle


    function_wrapper_type thread_init(){
        return [this](){

            while (_running || _should_finish_before_exit)
            {
                std::unique_lock<std::mutex> lk{_work_mutex};
                _cv.wait(lk,[this](){
                    return !_running || !_work.empty() || _forced_idle;
                });
                //if we are not idle
                if (!_forced_idle) {

                    //if the pool is not still running
                    if (!_running)
                    {
                        //do we have to finish all work before exiting
                        if (_should_finish_before_exit)
                        {
                            //is there work
                            if(!_work.empty()){
                                auto task = _work.front();
                                _work.pop_front();
                                //did i just finish the work?
                                if (_work.empty())
                                {
                                    //if so the we dont have to keep working
                                    _should_finish_before_exit=false;
                                }
                                lk.unlock();
                                task();
                            }
                            //no work so exit
                            else
                            {
                                _should_finish_before_exit=false;
                                lk.unlock();
                                _cv.notify_all();
                                break;
                            }
                        }
                        //dont have to finish work before exit
                        else
                        {
                            lk.unlock();
                            _cv.notify_all();
                            break;
                        }
                    }
                    //the pool is running
                    else{
                        //is ther eany work
                        if(!_work.empty())
                        {
                            auto task = _work.front();
                            _work.pop_front();
                            lk.unlock();
                            task();
                        }
                    }
                }
            }
        };
    }

};


using thread_pool = basic_thread_pool<std::deque,std::allocator>;




#endif /* thread_pool_h */
