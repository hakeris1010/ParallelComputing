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
    Data data;

public:
    std::atomic< long > label;

    // TODO: Something like this to track start node for the thresholded comparison.
    //LabeledNode<Data>* regionsFirstNode;

    volatile bool isReady;
    //std::condition_variable var;

    // Copy and move constructors.
    LabeledNode( const Data& _data, long _label = 0, bool _ready = true )
        : data( _data ), label( _label ), isReady( _ready )
    {}

    LabeledNode( Data&& _data, long _label = 0, bool _ready = true )
        : data( std::move( _data ) ), label( _label ), isReady( _ready )
    {}

    /*! Returns the const reference to data value of this node.
     *  - Assumes that this node's object will be alive longer than the 
     *    reference will be used.
     */ 
    const Data& getData() const {
        return data;
    }
};

/*! Structure for traversing the N-Dimensional Array/vector, 
 *  interpreting it as a graph, with custom neighbor getting function.
 *  - Is not thread safe. Must be assured that only one thread is manipulating 
 *    the traverser.
 *  @param NeighborGetter is a function of type:
 *      std::vector< size_t > neighborGetter( size_t currIndex, 
 *                                            const std::vector< size >& currCoords )
 *      - Returns already computed indexes of all neighbors, based on coordinates of 
 *        current element.
 */ 
template< class Element, class NeigborGetter >
class MatrixGraphTraverser{
protected:
    const std::vector< Element >& matrix;
    std::vector< size_t > dimensions;
    std::vector< size_t > coords;
    size_t index;

    // Function receiving coordinates of current element, and returning indexes of
    // neighboring elements. The indexes are computed autonomously.
    NeighborGetter getNeighborIndexes;

    bool checkValidity(){
        assert( dimensions.size() == coords.size() );
        for( size_t i = 0; i < coords.size(); i++ ){
            assert( coords[ i ] < dimensions[ i ] );
        }
    }

public:
    /*! Compute index of the element in N-dim vector.
     * Example 3D coordinates:
     * (x,y,z) = (1, 2, 3). 
     * - (Counting from 0).
     *
     * coord = X*Y*(z) + X*(y) + x;
     * - X, Y and Z are the dimensions of a hypercube vector.
     *
     * @param mg - the traverser object with already set coords and dimensions.
     */ 
    static size_t getIndexFromCoords( const std::vector< size_t >& coords,
                                      const std::vector< size_t >& dimensions )
    {
        size_t index = 0;
        for( size_t i = coords.size() - 1; i >= 0; i-- ){
            size_t tmp = 1;
            for( size_t j = 0; j < i; j++ ){
                tmp *= dimensions[ j ];
            }
            tmp *= coords[ i ];

            index += tmp;
        }
        return index;
    }

    /*! Compute index in the N-dim vector from given coordinates.
     * The coordinate 'i' is equal to index in the base 'i', divided by the base.
     * E.G. array dims: 8x3x3
     *      Index: 26.
     * 1. Base for coord. Z: baseZ = X*Y = 8*3 = 24.
     * 2. The coord Z: coordZ = indZ / baseZ = 26 / 24 = 1.
     * 3. Index in the next base (Y): indY = 26 % 24 = 2.
     * Repeat. 
     *
     * @param mg - the traverser object with already set dimensions and index.
     */ 
    static std::vector< size_t > getCoordsFromIndex( size_t ind, 
        const std::vector< size_t >& dimensions )
    {
        std::vector< size_t > coords( dimensions.size() );

        for( size_t i = coords.size() - 1; i >= 0; i-- ){
            // Compute the base of coordinate i.
            size_t base = 1;
            for( size_t j = 0; j < i; j++ ){
                base *= mg.dimensions[ j ];
            }

            // Compute coords and index in the next base.            
            coords[ i ] = ind / tmp;
            ind = ind % base;
        } 

        return coords;
    }

    /*! Copy and move constructors.
     *
     * @param matrix - reference to an existing vector of elements
     * TODO: Construct missing elements (coords or index).
     */ 
    MatrixGraphTraverser( std::vector< Element >& _matrix, 
                          const std::vector<size_t>& _dimensions,
                          size_t _index,
                          std::vector<size_t>&& _coords = std::vector<size_t>(),
                          NeighborGetter neighGet = defaultNeighGetter )
        : matrix( _matrix ), dimensions( _dimensions ), coords( std::move( _coords ) ),
          index( _index ), getNeighborIndexes( neighGet )
    {}
    //{ getIndexFromCoords(); }
    //{ checkValidity(); }

    MatrixGraphTraverser( std::vector< Element >& _matrix, 
                          std::vector<size_t>&& _dimensions,
                          size_t _index,
                          std::vector<size_t>&& _coords = std::vector<size_t>(),
                          NeighborGetter neighGet = defaultNeighGetter )
        : matrix( _matrix ), dimensions( std::move( _dimensions ) ), 
          coords( std::move( _coords ) ), index( _index ), 
          getNeighborIndexes( neighGet )
    {}

    // Force move constructor.
    MatrixGraphTraverser( MatrixGraphTraverser< Element, NeigborGetter >&& ) = default; 

    // Returns a Reference to object at this traverser's index.
    Element& getValue() const {
        return matrix[ index ];
    }

    // Use Copy Elision feature of C++11 and newer versions to avoid copies.
    /*virtual std::vector< Element& > getNeighborElements() const = 0;
    virtual std::vector< const Element& > getNeighborElementsConst() const = 0;

    virtual std::vector< MatrixGraphTraverser > getNeighborTraversers() const = 0;*/

    void getNeighborElements( std::vector< Element& >& vec ) const {
        std::vector< size_t > neighInds = getNeighborIndexes( index, coords );
        for( size_t i : neighInds ){
            // Push a Reference to an element to a vector.
            if( i < matrix.size() )
                vec.push_back( matrix[ i ] );
        }
    }

    void getNeighborElementsConst( std::vector< const Element& >& vec ) const {
        std::vector< size_t > neighInds = getNeighborIndexes( index, coords );
        for( size_t i : neighInds ){
            // Push a Const Reference to an element to a vector.
            if( i < matrix.size() )
                vec.push_back( (const Element&)( matrix[ i ] ) );
        }
    }

    void getNeighborTraversers( std::vector< MatrixGraphTraverser >& vec ) const {
        std::vector< size_t > neighInds = getNeighborIndexes( index, coords );
        for( size_t i : neighInds ){
            // Create a Traverser, and push it to the thing.
            if( i < matrix.size() ){
                vec.push_back( MatrixGraphTraverser< Element, NeigborGetter >(
                    matrix,
                    dimensions,
                    index,
                    getCoordsFromIndex( i, dimensions ),
                    getNeighborIndexes
                ) );
            }
        }
    }

};


/*! Class used for multithreaded component labeling.
 */ 
template< class Data, typename NeigborGetter = decltype( 2D_4Connection_Vector ) >
class ConnectedComponentLabeler {
private:
    // The reference values for every label.
    std::map< long, const Data& > labelDataValues;

    // Every label's equal label vector reference
    std::map< long, std::vector< long >& > equalityMap;

    // Equality storage - the labels which are equal.
    std::vector< std::vector< long > > equalities;

    // Protect the state.
    std::mutex mut;

public:
    ConnectedComponentLabeler( size_t threadCount,  
                               std::vector< LabeledNode< Data > > data, 
                               NeighborGetter neighGet )
    { }
};


template< class Data >
void labelStructure( ConnectedComponentLabeler& globalData, 
                     MatrixGraphTraverser< LabeledNode<Data> > currNode, 
                     Data threshold, size_t scannedNodeCount )
{
    // Vector for storing neighbors of current element.
    std::vector< LabelledNode<Data>& > neighs;
    neighs.reserve( 8 );

    // Storing the labels of the neighbors which are similar compared to current element.
    std::vector< long > similarLabels;
    similarLabels.reserve( 8 );

    // TODO: Track the current sector and use it's start value to manage thresholds.
    //const Data& startData = currNode.getValue().getData();

    // The traverser which we'll use for traversing consequent elements - currNode.
    for( size_t i = 0; i < scannedNodeCount; i++ ){
        // If label's already set, go to the next element.
        if( currNode.getValue().label.get() != 0 ) 
            continue;

        // Label is not set. Get neighbors.
        neighs.clear();
        similarLabels.clear();

        // Get neighbors (const version - for thr34d s4f3ty).
        currNode.getNeighborElementsConst( neighs );

        // Check all neigbors. If we find one similar to ours, do labeling.
        long ourLabel = 0;
        for( auto&& n : neighs ){
            if( n.getData().isSimilar( startData, threshold ) ){
                long theirLabel = n.label.get();

                // If the element already has a label, assign it's label to us.
                if( !ourLabel && theirLabel ){
                    ourLabel = theirLabel;
                }

                similarLabels.push_back( theirLabel );
            }
        }

        // If no available labels found (no similar neighbors ), assign a new label.
        if( !ourLabel )
            ourLabel = globalData->acquireNewLabel();

        // Set it to our element.
        currNode.getValue.label.set( ourLabel );

        // Create a new similarity list if we've encountered different similar labels.
        if( similarLabels.size() > 1 ){
            // Call new similarity create function, which fixes stuff and does what's needed.
            globalData->createNewSimilarity_Checked( ourLabel, similarLabels );
        }
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



