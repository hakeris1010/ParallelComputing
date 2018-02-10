#include <chrono>

/*!
 * Template functions for benchmarking function's execution time.
 */ 

namespace gtools{

const int CALLBACK_EVERY_ITERATION = 1;
const int CALLBACK_ONLY_AT_THE_END = 2;

template<typename... Args>
inline auto functionExecTimeReturnCallback( auto&& func, size_t times, auto&& callback, 
                                     int callbackFlags, Args&&... functionArgs ){
    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    if(callbackFlags & CALLBACK_EVERY_ITERATION){
        for(size_t i = 0; i < times; i++ ){
            callback( func( std::forward<Args>(functionArgs)... ) );
        }
    }
    else if(callbackFlags & CALLBACK_ONLY_AT_THE_END){
        for(size_t i = 0; i < times - 1; i++ ){
            func( std::forward<Args>(functionArgs)... );
        }
        callback( func( std::forward<Args>(functionArgs)... ) );
    }
    else{
        for(size_t i = 0; i < times; i++ ){
            func( std::forward<Args>(functionArgs)... );
        } 
    }

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
    //std::cout << "f() took " << time_span.count() << " ms\n";

    return time_span;
}

template<typename... Args>
inline auto functionExecTimeRepeated( auto&& func, size_t times, Args&&... args ){
    using namespace std::chrono;
    high_resolution_clock::time_point t1 = high_resolution_clock::now();

    for(size_t i = 0; i < times; i++ ){
        func( std::forward<Args>(args)... );
    }

    high_resolution_clock::time_point t2 = high_resolution_clock::now();
    duration<double> time_span = duration_cast<duration<double>>(t2 - t1);
    //std::cout << "f() took " << time_span.count() << " ms\n";

    return time_span;
}

template<typename... Args>
inline auto functionExecTime( auto&& func, Args&&... args ){
    return functionExecTimeRepeated( func, 1, std::forward<Args>(args)... );
}

}

