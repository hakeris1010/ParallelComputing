#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include "execution_time.hpp"

// Structure to store node's data.
template<class Data>
struct GraphNode{
    //const Data& data;

    std::vector< GraphNode > neighbors;
    std::atomic< long > label;

    volatile bool isReady;
    std::condition_variable var;

    GraphNode(){}
};

/*void boo(){
    double a = 0.000000001;
    for(int i=0; i<1000; i++){
        a += 0.3335/((double)i);
        double b = a*3.141592;
    }
}*/

void foo( size_t count ){
    GraphNode<int>* nodes[ count ];

    for( size_t i=0; i < count; i++ ){
        nodes[i] = new GraphNode<int>();
    }

    for( size_t i=0; i < count; i++ )
        delete nodes[i];
}

void foo2( size_t count ){
    for( size_t i=0; i < count; i++ )
        GraphNode<int> node;
}

int main(){
    size_t count = 100000;

    std::cout<< "foo() took "<< gtools::functionExecTime( foo, count ).count() <<" s\n";
    std::cout<< "foo2() took "<< gtools::functionExecTime( foo2, count ).count() <<" s\n";

    return 0;
}

