#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cassert>
#include "execution_time.hpp"

template<class Element>
class LabeledNodeBase {
protected:
    LabeledNodeBase( long _label = 0, bool _ready = true ) : 
        label( _label ), isReady( _ready )
    {}

public:
    // Label assigned to node.
    std::atomic< long > label;

    // Synchronization primitives for managing the dynamic loading of data.
    volatile bool isReady;
    std::condition_variable var; 

    virtual int getNeigbors( std::vector< LabeledNodeBase<Element>* >& neighs ) const = 0;
    virtual Element& getData() = 0;
    virtual const Element& getData() const = 0;
};

// Structure to store node's data.
template<class Data>
class GraphNode : public LabeledNodeBase<Data>{
private:
    // A reference to a memory location storing the data of this node.
    const Data& data; 
     
    // Pointers to neighboring objects.
    const std::vector< std::shared_ptr< GraphNode<Data> > > neighbors;

public:
    GraphNode( const Data& _data, 
               std::initializer_list< std::shared_ptr< GraphNode<Data> > >&& _neighs,
               long _label = 0,
               bool _ready = true )
        : data( _data ), neighbors( std::move( _neighs ) ), 
          LabeledNodeBase<Data>( _label, _ready )
    {}

    GraphNode( const Data& _data, 
               std::vector< std::shared_ptr< GraphNode<Data> > >&& _neighs,
               long _label = 0,
               bool _ready = true )
        : data( _data ), neighbors( std::move( _neighs ) ),
          LabeledNodeBase<Data>( _label, _ready )
    {}

    int getNeigbors( std::vector< LabeledNodeBase<Data>* >& neighs ) const {
        for( auto&& n : neighbors ){
            // Get raw pointers from the shared ptrs.
            neighs.push_back( n.get() );
        }
    }

    const Data& getData() const {
        return data;
    }
};


template<class Element, typename Neighborhood>
class VectorNode : public LabeledNodeBase<Element>{
private:
    const std::vector< VectorNode >& container;
    const size_t index;

    const std::function< void( const std::vector< Element >&, size_t, 
                   std::vector< LabeledNodeBase<Element>* >& > neighFinder;

public:
    VectorNode( std::vector< Element >& cont, size_t ind, const Neighborhood lambda,
                long _label = 0, bool _ready = true ) : 
        container( cont ), index( ind ), LabeledNodeBase<Element>( _label, _ready )
    { assert( index <= container.size() ); }

    int getNeigbors( std::vector< LabeledNodeBase<Element>* >& neighs ) const = 0;

    const Element& getData() const {
        return container[ index ];
    }
};


// Structure to store node's data.
template<class Data>
struct SimpleLabeledNode{
private:
    Data data;

public:
    std::atomic< long > label;

    volatile bool isReady;
    std::condition_variable var;

    // Copy and move constructors.
    LabeledNode( const Data& _data, long _label = 0, bool _ready = true )
        : data( _data ), label( _label ), isReady( _ready )
    {}

    LabeledNode( Data&& _data, long _label = 0, bool _ready = true )
        : data( std::move( _data ) ), label( _label ), isReady( _ready )
    {}
};

template< class Element >
class MatrixGraphTraverserConst{
private:
    const std::vector< Element >& matrix;
    const std::vector< size_t > dimensions;

public:
    /*! Copy and move constructors.
     * @param matrix - reference to an existing vector of elements
     */ 
    MatrixGraphTraverser( std::vector< Element >& matrix, 
                          const std::vector<size_t>& dimensions )
        : matrix( _matrix ), dimensions( _dimensions )
    {}

    MatrixGraphTraverser( std::vector< Element >& matrix, 
                          std::vector<size_t>&& dimensions )
        : matrix( _matrix ), dimensions( std::move( _dimensions ) )
    {} 

}



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
};

/*
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
*/

int main(){
    size_t count = 100000;

    //std::cout<< "foo() took "<< gtools::functionExecTime( foo, count ).count() <<" s\n";
    //std::cout<< "foo2() took "<< gtools::functionExecTime( foo2, count ).count() <<" s\n";

    return 0;
}



