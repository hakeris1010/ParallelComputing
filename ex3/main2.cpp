#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <cctype>

struct Image{
    size_t width;
    size_t heigth;
    std::vector<uint32_t> data;
};

const Image testImage = {
    8, 6,
    std::vector<uint32_t>({ 
        1, 1, 1, 1, 0, 0, 0, 0,
        1, 1, 1, 0, 0, 0, 1, 1,
        1, 0, 1, 1, 0, 1, 1, 0,
        0, 0, 1, 0, 0, 1, 0, 0, 
        1, 0, 0, 0, 0, 0, 0, 0, 
        1, 0, 0, 0, 0, 0, 0, 0
    })
};

// Structure to store node's data.
template<class Data>
struct LabeledNode{
    const Data& data;
    std::atomic< long > label;

    std::vector< LabeledNode* > neighs;

    volatile bool isReady;
    std::condition_variable var;

    LabeledNode( const Data& _data, long _label = 0, bool _ready = true )
        : data( _data ), label( _label ), isReady( _ready )
    {}

    LabeledNode( Data&& _data, long _label = 0, bool _ready = true )
        : data( std::move( _data ) ), label( _label ), isReady( _ready )
    {}
};

/*! Create a labeling-ready node vector.
 */  
bool createLabeledVector< T


int main(){
    return 0;
}



