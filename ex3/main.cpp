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


/*! Structure to store labeled node's data.
 *  - Vector of these objects is allocated at the beginning of the job.
 *    It's a long-term memory allocation. Usually on the heap.
 *  - However to turn this vector into a graph we use traversing object,
 *    which is allocated on the stack at each iteration, and not kept in memory
 *    for long.
 */ 
template<class Data>
struct LabeledNode{
private:
    const Data data;

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
class MatrixGraphTraverser{
protected:
    const std::vector< Element >& matrix;
    const std::vector< size_t > dimensions;
    const std::vector< size_t > coords;

public:
    /*! Copy and move constructors.
     * @param matrix - reference to an existing vector of elements
     */ 
    MatrixGraphTraverser( std::vector< Element >& _matrix, 
                          const std::vector<size_t>& _dimensions,
                          const std::vector< size_t > coords,
                          size_t _index )
        : matrix( _matrix ), dimensions( _dimensions ), index( _index )
    { assert( index < matrix.size() ); }

    MatrixGraphTraverser( std::vector< Element >& _matrix, 
                          std::vector<size_t>&& _dimensions,
                          size_t _index )
        : matrix( _matrix ), dimensions( std::move( _dimensions ) ), index( _index )
    { assert( index < matrix.size() ); } 

    // Returns a Reference to object at this traverser's index.
    Element& getValue() const {
        return matrix[ index ];
    }

    virtual bool getNeighbors( std::vector< Element& >& neighs ) const = 0;
};

template< class Element >
class MatrixGraphTraverser4Connection : public MatrixGraphTraverser<Element> {
public:
    using MatrixGraphTraverser<Element>::MatrixGraphTraverser;

    bool getNeighbors( std::vector< Element& >& neighs ) const {
        for( auto dim : this->dimensions ){
            
        }
    }
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
        //return red == p.red && green == p.green && blue == p.blue && alpha == p.alpha;
        return *((uint32_t*) &red) == *((uint32_t*) &(p.red));
    }

    bool operator<( const Pixel& p ){
        //return red < p.red || green < p.green || blue < p.blue || alpha < p.alpha;
        return *((uint32_t*) &red) < *((uint32_t*) &(p.red));
    }

    bool isSimilar( const Pixel& p, char thr ){
        return abs(red - p.red) <= thr   && abs(green - p.green) <= thr && 
               abs(blue - p.blue) <= thr && abs(alpha - p.alpha) <= thr;
    }
};


template< class Data >
void labelStructure( MatrixGraphTraverser< LabeledNode<Data> > startNode, Data threshold, size_t scannedNodeCount ){
    neighs.reserve( 8 );

    MatrixGraphTraverser< LabeledNode<Data> > currNode = startNode; 

    for( size_t i = 0; i < scannedNodeCount; i++ ){
        std::vector< const LabelledNode<Data>& > neighs;

        currNode.getNeighbors( neighs, threshold );
        if( neighs.size() > 0
    }
}



/*void oneDimVector( std::vector<size_t> dimensions, size_t repeats ){
    // Set size of the matrix vector and coordinates of the element to be
    // accessed each time.
    size_t size = 1;
    std::vector<size_t> coords( dimensions.size() );

    for( size_t i = 0; i < dimensions.size(); i++ ){
        size *= dimensions[i];

        // We'll be accessing the central element.
        coords[i] = dimensions[i] / 2;
    }

    // N-dimensional matrix vector.
    std::vector<int> vect( size, 5 );

    // Access the element in N-dim vector.
    // Example 3D coordinates:
    // (x,y,z) = (1, 2, 3). 
    // - (Counting from 0).
    //
    // coord = X*Y*(z) + X*(y) + x;
    // - X, Y and Z are the dimensions of a hypercube.
    //
    for( size_t i = 0; i < repeats; i++ ){
        size_t index = 1;
        for( size_t i = 0; i < dimensions.size(); i++ ){
            index = coords[i]* 
        }
    }
} */

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

int main(){
    size_t reps = 10000000;
    size_t wid = 10;
    size_t hei = 20;

    std::cout<< "foo() took "<< gtools::functionExecTime( 
        oneDimVector, wid, hei, reps ).count() <<" s\n";
    std::cout<< "foo2() took "<< gtools::functionExecTime( 
        twoDimVector, wid, hei, reps ).count() <<" s\n";

    return 0;
}



