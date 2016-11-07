#include "thread_pool.hpp"

int main(int argc, char** argv)
{
    thread_pool pool;
    pool.push([](){
        return 0;
    });

    return 0;
}
