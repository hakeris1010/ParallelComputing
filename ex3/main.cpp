/********************************************
 * Connected Component Labeling utility.    * 
 * Version v0.1pre                          *
 *                                          *
 ********************************************/ 

/*! Algorithm summary.
 * - TODO: NOTE: The whole summary is VERY PRELIMINARY. No actual work has been done yet!
 *
 * - Program uses greatly modified Two-Pass component labeling algorithm, 
 *   redesigned to support parallelization.
 *
 * ================================================
 *
 * A Simplified sequence of steps:
 *
 * 1. The LabeledNode matrix is initialized from the image matrix.
 *
 * 2. A number of labeler threads are assigned to corresponding square regions in 
 *    the matrix, and start the First Pass on it.
 *    - The region sizes are determined by the optimal relation between the size of
 *      the problem and thread count (yet to be determined).
 *  
 *    2.1. First pass is modified to take Threshold into account. Individual label
 *         values aren't stored in each node. Instead, each node of the region stores a
 *         reference to the Original Node of the region, which stores the label of the
 *         region.
 *    2.3. So, if equality between regions is encountered, only the Original Node's label
 *         needs to be changed, eliminating the need of additional equality containers.
 *
 *       - However minimal equality containers would be needed in case of equalities 
 *         between the regions. However investigation is needed there.
 *
 * 3. When labeler thread completes the First pass on a square, it notifies the 
 *    Control thread that region is labeled.
 *    The Control thread waits until 2 neighboring regions are labeled, then runs
 *    Equality Checker thread on the neighboring regions border.
 *    When 4 neighboring regions are complete, the corner case is being examined.
 *  
 *  - These threads are optional though, it's possible to run those border-checks at the 
 *    very end, when all labelers complete. There more investigation is needed.
 *
 * In summary, if maximum parameters are specified, there would be these threads working:
 * 1. N labeler threads.
 *    - Are launched by the control thread, to label their corresponding regions.
 *
 * 2. Control Thread (usually main initial thread):
 *    2.1. Launches labeler threads on every region to be parallely labeled.
 *    2.2. Launches the Equality Checker thread. It's suspended at the beginning.
 *    2.2. After launching 'em, waits until some neighboring regions' labeling is completed.
 *       - If so, notifies Equality Checker thread to check for equal labels in the border
 *         and assign same label to them.
 *
 * 3. Equality Checker thread.
 *    - Checks for equalities on borders between regions and fixes them.
 *    - Wakes up when notified by the Control Thread.
 *    - Can be implemented by inner labeling algorithm which runs on a region specified.
 *
 * ================================================
 *
 * The Labeling Algorithm:
 * - TODO: More investigation needed.
 * - Is a version of Two-Pass algorithm modified to be flexible.  
 * - Algorithm can process N-dimensional matrices - the algotithm is based upon
 *   gtools::MatrixGraphTraverser - the traverser which interfaces every matrix to a
 *   graph-like structure.
 */ 


#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <cassert>
#include <unordered_set>
#include "MatrixGraphTraverser.hpp"

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

namespace gtools{ extern void testTraverser(); }
namespace bench{ extern void launchAllBenchmarks(); }

int main(){
    gtools::testTraverser();
    bench::launchAllBenchmarks();

    return 0;
}



