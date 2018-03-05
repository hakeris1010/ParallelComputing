#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cassert>
//#include <functional>
#include "execution_time.hpp"


// Structure used on our project.
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
 *  - The Default Getter is Checked - to ensure maximum optimizeishon.
 *  - Takes a Pass-By-Value coordinates, because this way we have only one copy,
 *    and modify the coordinates on the go, with every neighbor being a tweaked value 
 *    in the same memory block.
 */ 
constexpr auto defaultMatrixNeighborGetter = []( 
        std::vector< size_t > coords,
        const std::vector< size_t >& startRegion,  
        const std::vector< size_t >& endRegion, 
        auto callback ) -> void 
{
    for( size_t i = 0; i < coords.size(); i++ ){
        // Increment and decrement current coordinate, threrefore access 2 neighbors
        // in current axis. Callback doesn't modify the coords - takes a const reference.
        // - We notify the callback that the value is "checked".
        if( coords[ i ] < endRegion[ i ] ){
            coords[ i ] += 1;
            callback( coords, true );
            coords[ i ] -= 1;
        }

        if( coords[ i ] > startRegion[ i ] ){
            coords[ i ] -= 1;
            callback( coords, true );
            coords[ i ] += 1;
        }
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
 *      void Callback( const std::vector< size_t >& neighborCoords, bool checkingDone = false )
 */ 
template< class Element, class NeighborGetter = decltype( defaultMatrixNeighborGetter ) >
class MatrixGraphTraverser{
protected:
    //======== Constant Matrix-Dependent properties ========//
    // A pointer to a contiguous memory block, which stores values in order 
    // simulating an N-dimensional matrix. The memory block could be taken from a 
    // std::vector, or from a C-array.
    // TODO: Make Constness-Independent iterator-type container.
    mutable Element* matrix;

    // Matrix properties: size of a memory block containing it, and dimensions of a matrix.
    const size_t matrixSize;
    const std::vector< size_t > dimensions;

    // Offset in the "matrix" vector, and end-offset, indicating an end of the region.
    // Both are inclusive - so endOffset values can be equal to offset values.
    // Passed as a C-tor parameters.
    const std::vector< size_t > offset;
    const std::vector< size_t > endOffset;

    // Precomputed index of first/last element in memory, if available region is contiguous.
    const size_t offsetIndex;
    const size_t endOffsetIndex;
    
    // Indicates whether coordinate value checks are needed or just index checks are OK.
    // Set to false when available region (endOffset - offset) is the whole matrix.
    const bool checkCoords;

    // Function receiving coordinates of current element, and returning indexes of
    // neighboring elements. The indexes are computed autonomously.
    const NeighborGetter getNeighborIndexes; 

    //======== Modifiable properties ========//
    
    // Current coordinates, in N-dimensional form.
    std::vector< size_t > coords;

    // Current coordinates in "vector index" form.
    size_t index;

    //======== Private Methods ========//

    /*! Perform full validity test on the state of the traverser.
     */ 
    bool checkValidityFull() const {
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

    /*! Returns the index indicating the end of the available region,
     *  if the available region is contiguous in memory from the start of the block.
     *  ( lastCoord != 0, otherCoords == maxCoord ).
     *  @return index of the end coord, if available region is contiguous,
     *          0 otherwise.
     */ 
    static size_t getStartEndIndexes( bool start,
            const std::vector< size_t >& offset, 
            const std::vector< size_t >& endOffset, 
            const std::vector< size_t >& dimensions )
    {
        // Check if offset is Zero and endOffset's coordinates are maximum for their dim.
        size_t blockSize = 1, i = 0;
        for( i = 0; i < endOffset.size() - 1; i++ ){
            if( (endOffset[ i ] != dimensions[ i ] - 1) || (offset[ i ] != 0) )
                return 0;
            blockSize *= dimensions[ i ];
        }
        // The last coordinate determines how many blocks the index goes through.
        if( start ){
            return blockSize * offset[ i ];
        }
        return ( blockSize * (endOffset[ i ] + 1) ) - 1;
    }

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
        for( size_t i = coords.size() - 1; i < coords.size(); i-- ){
            size_t tmp = 1;
            for( size_t j = 0; j < i; j++ ){
                tmp *= dimensions[ j ];
            }
            //tmp *= (coords[ i ] + offset[ i ]);
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
                base *= dimensions[ j ];
            }
            // Compute coords and index in the next base.            
            //coords[ i ] = (ind / base) - offset[ i ];

            coords[ i ] = (ind / base);
            ind = ind % base;
        } 

        return coords;
    }

    /*! More optimized and "checked" non-static index getter.
     *  - Calculates the index from current coords, while at the same time, checking
     *    if it's valid.
     *  @param _coords - N-dimensional coordinates of the element.
     *  @param a reference to the variable to store index in.
     *  @return true, if calculated index is valid.
     */ 
    bool getIndexFromCoords_Checked( const std::vector< size_t >& _coords, 
                                     size_t& index ) const {
        index = 0;
        for( size_t i = _coords.size() - 1; i < _coords.size(); i-- ){
            // Check for this axis's coord's validity (is in bounds).
            if( _coords[ i ] < offset[ i ] || _coords[ i ] > endOffset[ i ] )
                return false;

            // Calculate the i'th block to be added.
            size_t tmp = 1;
            for( size_t j = 0; j < i; j++ ){
                tmp *= dimensions[ j ];
            }
            tmp *= _coords[ i ]; 
            index += tmp;
        }
        // Success. Index is valid!
        return true;
    }

public:
    /*! Copy and move constructors.
     *
     * @param matrix - reference to an existing vector of elements
     * TODO: Construct missing elements (coords or index).
     */ 
    MatrixGraphTraverser( Element* _matrix, size_t _matrixSize, 
                          std::vector<size_t>&& _dimensions,
                          std::vector<size_t>&& _coords     = std::vector<size_t>(),
                          std::vector<size_t>&& _offset     = std::vector<size_t>(),
                          std::vector<size_t>&& _endOffset  = std::vector<size_t>(),
                          bool useCoordChecking             = true, 
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
          offsetIndex( getStartEndIndexes( true, offset, endOffset, dimensions ) ),
          endOffsetIndex( getStartEndIndexes( false, offset, endOffset, dimensions ) ),
          checkCoords( useCoordChecking ? true : endOffsetIndex == 0 ),
          getNeighborIndexes( neighGet )
    { 
        // Initialize index now because it depends on other properties.
        index = getIndexFromCoords( coords, dimensions );

        // Check the validity of all properties to avoid errors later.
        checkValidityFull(); 
    }

    /*! Vector-data constructor. Uses data from the vector as matrix.
     */ 
    MatrixGraphTraverser( std::vector<Element>& _matrix, 
                          std::vector<size_t>&& _dimensions,
                          std::vector<size_t>&& _coords     = std::vector<size_t>(),
                          std::vector<size_t>&& _offset     = std::vector<size_t>(),
                          std::vector<size_t>&& _endOffset  = std::vector<size_t>(),
                          bool useCoordChecking             = true, 
                          NeighborGetter neighGet           = defaultMatrixNeighborGetter
                        )    
        : MatrixGraphTraverser( &(_matrix[0]), _matrix.size(), std::move( _dimensions ), 
           std::move( _coords ), std::move( _offset ), std::move( _endOffset ), 
           useCoordChecking, neighGet )
    {}

    // Force move constructor - but just for the DEBUG stage, to ensure no copying is done.
    //MatrixGraphTraverser( MatrixGraphTraverser< Element, NeighborGetter >&& ) = default; 
    //MatrixGraphTraverser( const MatrixGraphTraverser< Element, NeighborGetter >& ) = delete; 

    /*! Returns a Reference to object at this traverser's index.
     *  - Const and Non-Const versions.
     *  - Assumes that "index" is correct.
     */ 
    Element& getValue() const {
        return matrix[ index ];
    }

    /*! Getters for state & property values.
     *  - Vector-ones use copying because of const-correctness.
     */ 
    size_t getIndex() const { return index; }
    std::vector< size_t > getCoords()     const { return coords; }
    std::vector< size_t > getDimensions() const { return dimensions; }
    std::vector< size_t > getOffset()     const { return offset; }
    std::vector< size_t > getEndOffset()  const { return endOffset; }

    bool usingContiguousRegion() const { return !checkCoords; }

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
    bool advance( const size_t direction = 0 ) {
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
        index = getIndexFromCoords( coords, dimensions );

        return advanced;
    }

    /*! Neighbor getters. 
     * A good approach is to use a Callable parameter to pass to neighbor getter.
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
     */
    void getNeighborElements( std::vector< std::reference_wrapper< Element > >& vec ) const
    {
        getNeighborIndexes( coords, offset, endOffset, 
        [ & ]( const std::vector<size_t>& neigh, bool checked = false ){
            // If neighborGetter hasn't already checked coordinates for us.
            if( !checked ){
                //std::cout<< "Non-Checked.\n";

                // Checking coords if available region is not contguous.
                if( this->checkCoords ){
                    size_t indx;
                    if( getIndexFromCoords_Checked( neigh, indx ) )
                        vec.push_back( this->matrix[ indx ] );
                    return;
                } 
                // However if available region is contiguous, we can only check
                // the indexed of the start and end of the region.
                size_t indx = getIndexFromCoords( neigh, this->dimensions );
                if( this->offsetIndex <= indx && indx <= this->endOffsetIndex ){
                    vec.push_back( this->matrix[ indx ] );
                }
                return;
            }
            //std::cout<< "Checked!\n";

            // Coords were already checked. We're sure index is in matrix's memscope.
            // If not, that's neighGetter's fault.
            size_t indx = getIndexFromCoords( neigh, this->dimensions );
            vec.push_back( this->matrix[ indx ] ); 
        } );
    }

    /*! Gets neighbors using C++11 Copy Elision (Returns by Value).
     *  @Return a vector of references to neighboring elements.
     */ 
    std::vector< std::reference_wrapper< Element > > getNeighborElements() const {
        std::vector< std::reference_wrapper< Element > > vec;
        getNeighborElements( vec );
        return vec;
    }

    /*! Gets traversers pointing to neighbors.
     *  TODO: Make the matrix-properties section of traverser be independent.
     */ 
    void getNeighborTraversers( std::vector< MatrixGraphTraverser< Element > >& vec ) const {
        getNeighborIndexes( coords, offset, endOffset, 
        [ & ]( const std::vector<size_t>& neigh, bool checked = false ){
            size_t indx;
            if( !checked ){
                // Checking coords if available region is not contguous.
                if( this->checkCoords ){
                    if( !getIndexFromCoords_Checked( neigh, indx ) )
                        return;
                }
                // Check only the indexes of the start and end of the region if contiguous.
                else { 
                    indx = getIndexFromCoords( neigh, this->dimensions );
                    if( indx < this->offsetIndex || indx > this->endOffsetIndex )
                        return;
                }
            }
            // Coords were already checked in the neigh.getter.
            else{
                indx = getIndexFromCoords( neigh, this->dimensions );
            }
            // At this point we're sure index is in matrix's memscope.
            // Copy current traverser, and only change the coords and index.
            MatrixGraphTraverser< Element > neighTrav( *this );
            neighTrav.coords = neigh;
            neighTrav.index = indx;

            // Move to vector.
            vec.push_back( std::move( neighTrav ) ); 
        } );
    }

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

// Helper for checking vectors of equal types.
template<typename T>
bool operator==( const std::vector< T >& vec, 
                 const std::vector< std::reference_wrapper<T> >& refVec ){
    if( vec.size() != refVec.size() )
        return false;
    for( size_t i = 0; i < vec.size(); i++ ){
        if( vec[ i ] != refVec[ i ].get() )
            return false;
    }
    return true;
}

template<typename T>
bool operator==( const std::vector< std::reference_wrapper<T> >& refVec, 
                 const std::vector< T >& vec ){
    return vec == refVec;
}

/*! Test framework for Matrix Graph Traverser.
 *  Includes:
 *  - Data Vector properties (dimensions and vector containing data)
 *  - Test case (ctor params and valid data to check against).
 */ 
template< typename T >
struct Test_MatrixGraphTraverser_DataVector{
    std::vector< T > data;
    std::vector< size_t > dimensions;

    Test_MatrixGraphTraverser_DataVector( std::vector< T >&& _data,
                                          std::vector< size_t >&& _dims )
       : data( std::move( _data ) ), dimensions( std::move( _dims ) )
    { } 
};

template< typename T, typename NG = decltype( defaultMatrixNeighborGetter ) >
struct TestCase_MatrixGraphTraverser{
    // Actual matrix.
    Test_MatrixGraphTraverser_DataVector< T >& refData;
    
    // MatrixGraphTraverser construction params
    std::vector< size_t > startCoords;
    std::vector< size_t > offset;
    std::vector< size_t > endOffset;
    bool useCoordChecking = true;
    NG neighGetter;

    // Testing-against expected values.
    std::vector< std::vector< T > > neighbors;
    std::vector< T > subRegionElements;
    bool contiguous;

    TestCase_MatrixGraphTraverser( 
            Test_MatrixGraphTraverser_DataVector< T >& _refData,
            bool _contiguous, NG _neighGetter = defaultMatrixNeighborGetter 
    )
     : refData( _refData ), neighGetter( _neighGetter ), contiguous( _contiguous )
    {}
};

// Tests advancement
template< typename T >
class Test_MatrixGraphTraverser{
private:
    TestCase_MatrixGraphTraverser< T > testCase;
    MatrixGraphTraverser< T > trav;

    void createTraverser(){
        trav = std::move( MatrixGraphTraverser< T >( 
            testCase.refData.data, 
            std::vector<size_t>( testCase.refData.dimensions ), 
            std::vector<size_t>( testCase.startCoords ),
            std::vector<size_t>( testCase.offset ), 
            std::vector<size_t>( testCase.endOffset ), 
            testCase.useCoordChecking,
            testCase.neighGetter
        ) );
    }

public:
    // Copy 'n' Move Constructors.
    Test_MatrixGraphTraverser( TestCase_MatrixGraphTraverser< T >&& tcase )
     : testCase( std::move( tcase ) ), trav(
        testCase.refData.data, 
        std::vector<size_t>( testCase.refData.dimensions ), 
        std::vector<size_t>( testCase.startCoords ),
        std::vector<size_t>( testCase.offset ), 
        std::vector<size_t>( testCase.endOffset ), 
        testCase.useCoordChecking,
        testCase.neighGetter
       ) 
    { }

    Test_MatrixGraphTraverser( const TestCase_MatrixGraphTraverser< T >& tcase )
     : testCase( tcase ), trav(
        testCase.refData.data, 
        std::vector<size_t>( testCase.refData.dimensions ), 
        std::vector<size_t>( testCase.startCoords ),
        std::vector<size_t>( testCase.offset ), 
        std::vector<size_t>( testCase.endOffset ), 
        testCase.useCoordChecking,
        testCase.neighGetter
       ) 
    { }
    
    void advance( int verbose = 0, size_t offset = 0 ){
        // Set reference to expected region values. If subregion's values weren't set,
        // it means we are checking the whole region.
        auto&& regionExpected = ( testCase.subRegionElements.empty() ?
                                  testCase.refData.data : testCase.subRegionElements );
        // Do it!
        for( size_t i = offset; i < regionExpected.size(); i++ ){
            if( verbose > 0 ){
                std::cout<< "[ i: "<< i <<" ]:\n value: "<< trav.getValue() << "\n index: "
                         << trav.getIndex() <<", coords: "<< trav.getCoords() <<"\n";
                if( verbose > 1 ){
                    std::cout<< " neighbors: "<< trav.getNeighborElements() <<"\n\n";
                }
            }

            // Check neighbors if been specified.
            if( i < testCase.neighbors.size() ){
                assert( testCase.neighbors[ i ] == trav.getNeighborElements() );
            }

            // Data and index must be compliant.
            assert( trav.getValue() == regionExpected[ i ] );
            if( testCase.contiguous )
                assert( trav.getIndex() == i );

            // If not last, advance must be successful
            if( i < regionExpected.size() - 1 )
                assert( trav.advance() );
            // If at the last element, advance must return false.
            else
                assert( !trav.advance() );
        }
    }

    void full( int verbose = 0 ){
        advance( verbose );
    }
};

// Tests the Traverser.
void testTraverser(){
    Test_MatrixGraphTraverser_DataVector< int > tcase( {
        1,  2,  3,  4,  5, 
        6,  7,  8,  9,  10, 
        11, 12, 13, 14, 15,
        16, 17, 18, 19, 20
    }, { 5, 4 } 
    );
    TestCase_MatrixGraphTraverser<int> testContiguous( tcase, true );

    TestCase_MatrixGraphTraverser<int> testSubregion( tcase, false );
    testSubregion.startCoords = { 1, 1 };
    testSubregion.offset = { 1, 1 };
    testSubregion.endOffset = { 3, 3 };
    testSubregion.subRegionElements = { 7, 8, 9, 12, 13, 14, 17, 18, 19 };

    {
        std::cout << "\n=====================\nTest contiguous version.\n";
        auto tester = Test_MatrixGraphTraverser<int>( testContiguous );
        tester.advance( 2 );
    }
    {
        std::cout << "\n=====================\nTest Subregion (1,1)-(3,3) version.\n";
        auto tester = Test_MatrixGraphTraverser<int>( std::move(testSubregion) );
        tester.advance( 2 ); 
    }
}

int main(){
    testTraverser();

    return 0;
}



