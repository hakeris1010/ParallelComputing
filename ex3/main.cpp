#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cassert>
#include "execution_time.hpp"

// Structure to store node's data.
template<class Data>
struct GraphNode{
    // A reference to a memory location storing the data of this node.
    const Data& data;

    std::vector< std::shared_ptr< GraphNode<Data> > > neighbors;
    std::atomic< long > label;

    volatile bool isReady;
    std::condition_variable var;

    GraphNode( const Data& _data, 
               std::initializer_list< std::shared_ptr< GraphNode<Data> > >&& _neighs,
               long _label = 0,
               bool _ready = true )
        : data( _data ), neighbors( std::move( _neighs ) ), label( _label ), isReady( _ready )
    {}

    GraphNode( const Data& _data, 
               std::vector< std::shared_ptr< GraphNode<Data> > >&& _neighs,
               long _label = 0,
               bool _ready = true )
        : data( _data ), neighbors( std::move( _neighs ) ), label( _label ), isReady( _ready )
    {}
};

template<class Element>
class GraphNodeBase {
public:
    virtual int getAccessibleNeigbors( std::vector< Element& >& neighs ) const = 0;
    virtual Element& getData() = 0;
    virtual const Element& getData() const = 0;
};


template<class Element>
class VectorGraphNode{
private:
    std::vector< Element >& container;
    size_t index;
    std::function< int( size_t, const std::vector< Element >&, std::vector<Element>& ) > 
        getNeighbors;

public:
    VectorGraphNode( std::vector< Element >& cont, size_t ind ) : 
        container( cont ), index( ind ) {}



};


struct Pixel{
    char red;
    char green;
    char blue;
    char alpha;

    Pixel( char r, char g, char b, char a = 0xFF )
        : red( r ), green( g ), blue( b ), alpha( a )
    {}

    Pixel( int32_t val )
        : red( (char)(val >> 24) ), green( (char)(val >> 16) ), 
          blue( (char)(val >> 8) ), alpha( (char)(val >> 0) )
    {}

    bool operator==( const Pixel& p ){
        return red == p.red && green == p.green && blue == p.blue && alpha == p.alpha;
    }

    bool operator<( const Pixel& p ){
        return red < p.red || green < p.green || blue < p.blue || alpha < p.alpha;
    }

    bool isSimilar( const Pixel& p, char thr ){
        return abs(red - p.red) <= thr   && abs(green - p.green) <= thr && 
               abs(blue - p.blue) <= thr && abs(alpha - p.alpha) <= thr;
    }
}

void footest( GraphNode& startNode ){
    start
}

/*void boo(){
    double a = 0.000000001;
    for(int i=0; i<1000; i++){
        a += 0.3335/((double)i);
        double b = a*3.141592;
    }
}*/

void foo( size_t count ){
    // Memory used less than 100 MB.
    assert( count * ( sizeof( std::shared_ptr< GraphNode<int> > ) + 
                      sizeof( GraphNode<int> ) ) < 100000000 );

    std::vector< std::shared_ptr< GraphNode<int> > > nodes( count );

    for( size_t i=0; i < count; i++ ){
        int d = 0;
        nodes.push_back( std::shared_ptr< GraphNode<int> >( new GraphNode<int>( d, {} ) ) );
    }
}

void foo2( size_t count ){
    // Memory used less than 100 MB.
    assert( count * sizeof( GraphNode<int> ) < 100000000 ); 

    int a = 0;
    GraphNode<int> root( a, {} );
    GraphNode<int>* tmp = &root;

    for( size_t i = 0; i < count; i++ )
    {
        tmp->neighbors.push_back( std::shared_ptr< GraphNode<int> >( new GraphNode<int>( a, {} ) ) );
        tmp = (tmp->neighbors[0]).get(); 
    }
}

int main(){
    size_t count = 100000;

    std::cout<< "foo() took "<< gtools::functionExecTime( foo, count ).count() <<" s\n";
    //std::cout<< "foo2() took "<< gtools::functionExecTime( foo2, count ).count() <<" s\n";

    return 0;
}



