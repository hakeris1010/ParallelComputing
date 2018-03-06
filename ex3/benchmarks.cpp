/*! Various benchmarks, related to current project.
 *  Currently includes benchmarking one dimensional vector vs 2 dimensional one.
 */ 

#include <iostream>
#include <vector>
#include <mutex>
#include "execution_time.hpp"

namespace bench{

void oneDimVector( size_t wid, size_t hei, size_t repeats ){
    // 2-dimensional matrix vector.
    std::vector<int> vect( wid*hei, 5 );
    size_t x = wid/2;
    size_t y = hei/2;

    // Access the element "repeats" times.
    for( size_t i = 0; i < repeats; i++ ){
        size_t index = y*wid + x;
        /*if( index >= vect.size() ){
            std::cout << "Index " << index << " is out of bounds!\n";
            break;
        }*/

        // Dereference a member.
        auto&& el = vect[ index ];
    }
}

void twoDimVector( size_t wid, size_t hei, size_t repeats ){
    // 2-dimensional matrix vector storing columns inside a row.
    std::vector< std::vector<int> > vect( wid, std::vector<int>( hei ) );
    size_t x = wid/2;
    size_t y = hei/2;

    // Access the element "repeats" times.
    for( size_t i = 0; i < repeats; i++ ){
        /*if( y >= vect.size() || x >= vect[0].size() ){
            std::cout << "Index " << index << " is out of bounds!\n";
            break;
        }*/

        // Dereference a member.
        auto&& el = vect[ x ][ y ];
    }
}

// Benchmarks vector dimensions.
void runDimBenches(){
    size_t reps = 10000000;
    size_t wid = 10;
    size_t hei = 20;

    std::cout<< "OneDimVector took "<< gtools::functionExecTime( 
        oneDimVector, wid, hei, reps ).count() <<" s\n";
    std::cout<< "TwoDimVector took "<< gtools::functionExecTime( 
        twoDimVector, wid, hei, reps ).count() <<" s\n\n";
}

// Benchmarks for creating mutexes.
void createMutex( size_t times ){
    for( size_t i = 0; i < times; i++ ){
        std::mutex mut;
        std::mutex* m = new std::mutex();
        delete m;
    }
}

template< size_t size >
struct ObjectWithSize{
    char mc[ size ];
};

void createSimpleObjectSizeofMutex( size_t times ){
    for( size_t i = 0; i < times; i++ ){
        // On Stack
        ObjectWithSize< sizeof(std::mutex) > ob;

        // On Heap
        ObjectWithSize< sizeof(std::mutex) >* mob = new ObjectWithSize< sizeof(std::mutex) >();
        delete mob;
    }
}

void benchMutex(){
    size_t times = 1000000;

    std::cout<< "Size of std::mutex : " << sizeof( std::mutex ) <<"\n";
    std::cout<< "Mutex creation "<< times <<" times took "<< gtools::functionExecTime( 
        createMutex, times ).count() <<" s\n";
    std::cout<< "Simple object creation "<< times <<" times took "<< gtools::functionExecTime( 
        createSimpleObjectSizeofMutex, times ).count() <<" s\n"; 
}

// Launches all benchmarks.
void launchAllBenchmarks(){
    runDimBenches();
    benchMutex();
}

}


