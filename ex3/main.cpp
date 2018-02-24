#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cassert>
#include "execution_time.hpp"

/*template<class Element>
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
*/

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
    std::mutex mut;

    // Label of this node. 
    long label = 0;

    // Original Node, used to track start node for the thresholded comparison.
    LabeledNode<Data>* regionsFirstNode = nullptr;

    volatile bool isReady = true;
    //std::condition_variable var;

    // Copy and move constructors.
    LabeledNode( const Data& _data, long _label = 0, LabeledNode<Data>* firstNode = nullptr )
        : data( _data ), label( _label ), regionsFirstNode( firstNode )
    {}

    LabeledNode( Data&& _data, long _label = 0, LabeledNode<Data>* firstNode = nullptr )
        : data( std::move( _data ) ), label( _label ), regionsFirstNode( firstNode )
    {}

    /*! Returns the const reference to data value of this node.
     *  - Assumes that this node's object will be alive longer than the 
     *    reference will be used.
     */ 
    const Data& getData() const {
        return data;
    }
};


/*! Default matrix-style neighbor getter.
 *  - A Neighbor is every element backwards/forwards from current element in a 
 *    coordinate axis.
 *  - For example, in 2D space, there are 4 neighbors: 
 *    (x-1, y), (x+1, y), (x, y+1), (x, y-1).
 */ 
constexpr auto defaultMatrixNeighborGetter = []( 
        const std::vector< size_t >& coords, auto callback ) -> void
{
    for( size_t i = 0; i < coords.size(); i++ ){
        std::vector<size_t> tmp = coords;

        // Increment and decrement current coordinate, threrefore access 2 neighbors
        // in current axis. Callback doesn't modify the tmp - takes a const reference.
        tmp[ i ] += 1;
        callback( tmp );

        tmp[ i ] -= 2;
        callback( tmp );
    }
};

/*! Structure for traversing the N-Dimensional Array/vector, 
 *  interpreting it as a graph, with custom neighbor getting function.
 *  - Is not thread safe. Must be assured that only one thread is manipulating 
 *    the traverser.
 *  @param NeighborGetter is a function of type:
 *
 *      void neighborGetter( const std::vector< size_t >& coords, Callback callback )
 *
 *      - Computes the coordinates of all neighbors, based on coordinates of 
 *        current element, and calls a "callback" on each iteration, passing the
 *        coordinate of the neighbor.
 *
 *      @param Callback - a callable, used by the inner workings of this class,
 *          getting the coordinates of a neighbor and performing jobs.
 *
 *      void Callback( const std::vector< size_t >& neighborCoords )
 */ 
template< class Element, class NeighborGetter = decltype( defaultMatrixNeighborGetter ) >
class MatrixGraphTraverser{
protected:
    // TODO: Use simple pointer instead of vector.
    // A reference to a vector, which stores values in order simulating an 
    // N-dimensional matrix, and dimensions of that matrix.
    const Element* matrix;
    const size_t matrixSize;
    const std::vector< size_t > dimensions;

    // Offset in the "matrix" vector, and end-offset, indicating an end of the region.
    // Both are inclusive - so endOffset values can be equal to offset values.
    // Passed as a C-tor parameters.
    const std::vector< size_t > offset;
    const std::vector< size_t > endOffset;

    // Modifiable properties.
    // Current coordinates, in N-dimensional form.
    std::vector< size_t > coords;

    // Current coordinates in "vector index" form.
    size_t index;

    // Function receiving coordinates of current element, and returning indexes of
    // neighboring elements. The indexes are computed autonomously.
    const NeighborGetter getNeighborIndexes;

    /*! Perform full validity test on the state of the traverser.
     */ 
    bool checkValidityFull(){
        // Check coordinate vectors.
        assert( dimensions.size() == coords.size() );
        assert( dimensions.size() == offset.size() );
        assert( dimensions.size() == endOffset.size() );

        // Check actual low-level index in the buffer.
        assert( index < matrixSize );

        // Check if dimensions are correct.
        size_t dimSize = 1;
        for( auto dim : dimensions )
            dimSize *= dim;

        assert( dimSize == matrixSize );
        
        // Check validities of offset coordinates.
        int offsetIneqs = 0;
        for( size_t i = 0; i < coords.size(); i++ ){
            // Offset coodinates must be lower than endOffset.
            assert( offset[ i ] <= endOffset[ i ] );

            // Log inequalities between coords, to check if the offsets were not equal.
            if( offset[ i ] != endOffset[ i ] )
                offsetIneqs++;

            // And of course must be inside the dimensional space.
            assert( offset[ i ] < dimensions[ i ] );
            assert( endOffset[ i ] < dimensions[ i ] );

            // Check if coordinates are between offsets.
            assert( coords[ i ] >= offset[ i ] );
            assert( coords[ i ] <= endOffset[ i ] );
        }

        // Offsets must not be equal.
        assert( offsetIneqs != 0 );
    }

    /*! Checks if current Coordinates (coords) are between the offset values.
     */ 
    static bool checkCoordValidity( const std::vector< size_t >& _coords,
                                    const std::vector< size_t >& _offset,  
                                    const std::vector< size_t >& _endOffset )
    {
        for( size_t i = 0; i < _coords.size(); i++ ){
            if( ( _coords[ i ] < _offset   [ i ] ) ||
                ( _coords[ i ] > _endOffset[ i ] ) )
                return false;
        }
        return true;
    }

    /*! Helps to Initialize endOffset to the end of matrix, if not set.
     */ 
    static std::vector< size_t > getLastElementCoords( 
            const std::vector< size_t >& dimensions )
    {
        std::vector<size_t> coords;
        for( auto d : dimensions ){
            coords.push_back( d - 1 );
        }
        return coords;
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
                                      const std::vector< size_t >& offset,  
                                      const std::vector< size_t >& dimensions )
    {
        size_t index = 0;
        for( size_t i = coords.size() - 1; i < coords.size(); i-- ){
            size_t tmp = 1;
            for( size_t j = 0; j < i; j++ ){
                tmp *= dimensions[ j ];
            }
            tmp *= (coords[ i ] + offset[ i ]);

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
        const std::vector< size_t >& offset,  
        const std::vector< size_t >& dimensions )
    {
        std::vector< size_t > coords( dimensions.size() );

        for( size_t i = coords.size() - 1; i >= 0; i-- ){
            // Compute the base of coordinate i.
            size_t base = 1;
            for( size_t j = 0; j < i; j++ ){
                base *= dimensions[ j ];
            }

            // Compute coords and index in the next base.            
            coords[ i ] = (ind / base) - offset[ i ];
            ind = ind % base;
        } 

        return coords;
    }

    /*! Copy and move constructors.
     *
     * @param matrix - reference to an existing vector of elements
     * TODO: Construct missing elements (coords or index).
     */ 
    MatrixGraphTraverser( const Element* _matrix, size_t _matrixSize,
                          std::vector<size_t>&& _dimensions,
                          std::vector<size_t>&& _coords     = std::vector<size_t>(),
                          std::vector<size_t>&& _offset     = std::vector<size_t>(),
                          std::vector<size_t>&& _endOffset  = std::vector<size_t>(),
                          NeighborGetter neighGet           = defaultMatrixNeighborGetter
                        )
        : matrix( _matrix ), matrixSize( _matrixSize ),
          dimensions( std::move( _dimensions ) ), 
          coords( _coords.empty() ? std::vector<size_t>( dimensions.size(), 0 ) : 
                                    std::move( _coords ) ), 
          offset( _offset.empty() ? std::vector<size_t>( dimensions.size(), 0 ) : 
                                    std::move( _offset ) ),  
          endOffset( _endOffset.empty() ? getLastElementCoords( dimensions ) :
                                          std::move( _endOffset ) ), 
          getNeighborIndexes( neighGet )
    { 
        // Initialize index now because it depends on other properties.
        index = getIndexFromCoords( coords, offset, dimensions );

        // Check the validity of all properties to avoid errors later.
        checkValidityFull(); 
    }

    MatrixGraphTraverser( const std::vector<Element>& _matrix, 
                          std::vector<size_t>&& _dimensions,
                          std::vector<size_t>&& _coords     = std::vector<size_t>(),
                          std::vector<size_t>&& _offset     = std::vector<size_t>(),
                          std::vector<size_t>&& _endOffset  = std::vector<size_t>(),
                          NeighborGetter neighGet           = defaultMatrixNeighborGetter
                        )    
        : MatrixGraphTraverser( &(_matrix[0]), _matrix.size(), std::move( _dimensions ), 
           std::move( _coords ), std::move( _offset ), std::move( _endOffset ), neighGet )
    {}

    // Force move constructor - but just for the DEBUG stage, to ensure no copying is done.
    MatrixGraphTraverser( MatrixGraphTraverser< Element, NeighborGetter >&& ) = default; 
    MatrixGraphTraverser( const MatrixGraphTraverser< Element, NeighborGetter >& ) = delete; 

    /*! Returns a Reference to object at this traverser's index.
     *  - Const and Non-Const versions.
     *  - Assumes that "index" is correct.
     */ 
    const Element& getValueConst() const {
        return matrix[ index ];
    }
    Element& getValue() {
        return matrix[ index ];
    }

    /*! Returns current coordinates and index.
     */ 
    std::vector< size_t > getCoords() const {
        return coords;
    }
    size_t getIndex() const {
        return index;
    }

    /*! Moves traverser's Current Element pointer forward:
     *  - At first, attempts to move in the specified direction, and
     *    if end of region is reached in this direction, advances by 1 in
     *    the higher direction, and resets current direction.
     *  - For example, let's move by 1 in X direction in 2-dimensional matrix.
     *    End of region is reached. So X now = 0, and Y = Y + 1.
     *
     *  @param direction - index of dimension by which to advance.
     *         Defaults to the first dimension (X axis).
     *  @return true if advanced forward successfully, false if reached the end. 
     *          If reached the end, the "coords" are reset to beginning offset.
     */ 
    bool advance( size_t direction = 0 ) {
        // Move to the beginning of this direction, if space left,
        // if not, reset current coordinate to zero, and move to the next direction.
        bool advanced = false;
        for( size_t i = direction; i < coords.size(); i++ ){
            if( coords[ i ] < endOffset[ i ] ){
                coords[ i ]++;
                advanced = true;
                break;
            }
            coords[ i ] = offset[ i ];
        }

        // Compute index for actual element access from a matrix buffer.
        index = getIndexFromCoords( coords, offset, dimensions );

        return advanced;
    }

    // TODO (Maybe): Use Copy Elision feature of C++11 and newer versions to avoid copies.
    /*virtual std::vector< Element& > getNeighborElements() const = 0;
    virtual std::vector< const Element& > getNeighborElementsConst() const = 0;
    virtual std::vector< MatrixGraphTraverser > getNeighborTraversers() const = 0;*/

    /*! Neighbor getters. 
     * FIXME: A good approach is to use a Callable parameter to pass to neighbor getter.
     *  - This Callable will be called when each neighbor coordinate is created,
     *    getting passed the same coordinate.
     *  - The Callable will manage index computation and will get element from buffer.
     *  - This way we have only one iteration through neighbor coordinates 
     *    (instead of two), and we have the ability to dynamically check the coordinates
     *    without the need to expose inner buffer to a neighbor getter.
     *
     * - This function calls a neighborGetter which will compute coordinates of 
     *   every neighbor and pass them to our callback. 
     *   Then we check them, and get elements.
     * @param vec - ready-to-use vector to which we push neighboring elements.
     * @param checkCoords - if set, function checks whether neighbor coordinates 
     *        aren't out of offset-endOffset scope.
     *        If not set, may return neighbors out of the mentioned scope. 
     */
    void getNeighborElements( std::vector< Element& >& vec, bool checkCoords = true ) const 
    {
        getNeighborIndexes( coords, [ & ]( const std::vector<size_t>& neigh ){
            // If we check coordinates, just compute index and get an elem 'coz it's valid.
            // This approach should be used when dealing with situations like
            // out of offset scope -> not neighbor.
            if( checkCoords ){
                if( !checkCoordValidity( neigh, this->offset, this->endOffset ) )
                    return;

                size_t indx = getIndexFromCoords( neigh, this->offset, this->dimensions );
                vec.push_back( this->matrix[ indx ] );
            }
            // However if coords are not check'd, check da index.
            else{
                size_t indx = getIndexFromCoords( neigh, this->offset, this->dimensions );
                if( indx < this->matrixSize ){
                    vec.push_back( this->matrix[ indx ] );
                }
            }
        } );
    }

    std::vector< Element& > getNeighborElements( bool checkCoords = true ) const {
        std::vector< Element& > vec;
        getNeighborElements( vec, checkCoords );
        return vec;
    }

    /*void getNeighborElementsConst( std::vector< const Element& >& vec ) const {
        std::vector< size_t > neighInds = getNeighborIndexes( index, coords );
        for( size_t i : neighInds ){
            // Push a Const Reference to an element to a vector.
            if( i < matrix.size() )
                vec.push_back( (const Element&)( matrix[ i ] ) );
        }
    }*/

    /*void getNeighborTraversers( std::vector< MatrixGraphTraverser >& vec ) const {
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
    }*/

};


/*! Class used for multithreaded component labeling.
 */ 
/*template< class Data, typename NeigborGetter = decltype( 2D_4Connection_Vector ) >
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
*/


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

void runDimBenches(){
    size_t reps = 10000000;
    size_t wid = 10;
    size_t hei = 20;

    std::cout<< "foo() took "<< gtools::functionExecTime( 
        oneDimVector, wid, hei, reps ).count() <<" s\n";
    std::cout<< "foo2() took "<< gtools::functionExecTime( 
        twoDimVector, wid, hei, reps ).count() <<" s\n";
}

// Prints a vector.
std::ostream& operator<< ( std::ostream& os, const std::vector< auto >& vec ){
    os <<"[ ";
    for( auto&& el : vec ){
        os << el <<", ";
    }
    os <<"]";
    return os;
}

// Test case data
template< typename T, typename NG = decltype( defaultMatrixNeighborGetter ) >
struct TestCase_MatrixGraphTraverser{
    std::vector< T > data;
    std::vector< size_t > dimensions;
    std::vector< size_t > startCoords;
    std::vector< size_t > offset;
    std::vector< size_t > endOffset;
    NG neighGetter;

    TestCase_MatrixGraphTraverser( 
            std::vector< T >&& _data,
            std::vector< size_t >&& _dimensions,
            std::vector< size_t >&& _startCoords = std::vector<size_t>(),
            std::vector< size_t >&& _offset      = std::vector<size_t>(),
            std::vector< size_t >&& _endOffset   = std::vector<size_t>(),
            NG _neighGetter                      = defaultMatrixNeighborGetter 
    )
     : data( std::move( _data ) ), dimensions( std::move( _dimensions ) ), 
       startCoords( std::move( _startCoords ) ), offset( std::move( _offset ) ),
       endOffset( std::move( _endOffset ) ), neighGetter( _neighGetter )
    {}

    TestCase_MatrixGraphTraverser( const TestCase_MatrixGraphTraverser< T >& ) = delete;
    TestCase_MatrixGraphTraverser( TestCase_MatrixGraphTraverser< T >&& ) = delete;
    TestCase_MatrixGraphTraverser<T>& operator=( const TestCase_MatrixGraphTraverser< T >& ) = delete;
    TestCase_MatrixGraphTraverser<T>& operator=( TestCase_MatrixGraphTraverser< T >&& ) = delete;
};

// Tests advancement
template< typename T >
class Test_MatrixGraphTraverser{
private:
    const TestCase_MatrixGraphTraverser< T >& testCase;
    MatrixGraphTraverser< T > trav;

public:
    Test_MatrixGraphTraverser( const TestCase_MatrixGraphTraverser< T >& tcase )
     : testCase( tcase ), trav( testCase.data, 
        std::vector<size_t>( testCase.dimensions ), std::vector<size_t>( testCase.startCoords ),
        std::vector<size_t>( testCase.offset ), std::vector<size_t>( testCase.endOffset ), 
        testCase.neighGetter )
    {}

    void checkCoords( const std::vector<size_t>& c1, const std::vector<size_t>& c2 ){
        assert( c1.size() == c2.size() );
        for( size_t i = 0; i < c1.size(); i++ ){
            assert( c1[ i ] == c2[ i ] );
        }
    }
    
    void advance( size_t offset = 0, int verbose = 0 ){
        for( size_t i = offset; i < testCase.data.size(); i++ ){
            if( verbose > 0 ){
                std::cout<< "[ i: "<< i <<" ]:\n value: "<< trav.getValue() << "\n index: "
                         << trav.getIndex() <<", coords: "<< trav.getCoords() <<"\n";
                if( verbose > 1 )
                    std::cout<< " neighbors: "<< trav.getNeighborElements() <<"\n";
            }

            // Data and index must be compliant.
            assert( trav.getValue() == testCase.data[ i ] );
            assert( trav.getIndex() == i );

            // If not last, advance must be successful
            if( i < testCase.data.size() - 1 )
                assert( trav.advance() );
            // If at the last element, advance must return false.
            else
                assert( !trav.advance() );
        }
    }

    void full( int verbose = 0 ){
        advance();
    }
};

// Tests the Traverser.
void testTraverser(){
    const TestCase_MatrixGraphTraverser< int > tcase( {
        1,  2,  3,  4,  5, 
        6,  7,  8,  9,  10, 
        11, 12, 13, 14, 15,
        16, 17, 18, 19, 20
    }, { 5, 4 } 
    );

    Test_MatrixGraphTraverser<int>( tcase ).full();

    /*MatrixGraphTraverser<int> trav( tvec, { 5, 4 }, { 0, 0 } );

    std::cout << trav.getCoords() <<"\n\n";
    std::cout << trav.getValue() <<"\n";

    trav.advance();

    std::cout << trav.getValue() <<"\n";
    std::cout << trav.getCoords() <<"\n\n";
    */
}

int main(){
    testTraverser();

    return 0;
}



