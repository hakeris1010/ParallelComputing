/*! Testing set for MatrixGraphTraverser.
 *  - Currently tests all basic most-used cases.
 */ 

#include <iostream>
#include <vector>
#include <unordered_set>
#include "MatrixGraphTraverser.hpp"

namespace gtools{

// Prints a vector.
std::ostream& operator<< ( std::ostream& os, const std::vector< auto >& vec ){
    os <<"[ ";
    for( auto&& el : vec ){
        os << el <<", ";
    }
    os <<"]";
    return os;
}
std::ostream& operator<< ( std::ostream& os, 
        const std::vector< std::reference_wrapper< auto > >& vec ){
    os <<"[ ";
    for( auto&& el : vec ){
        os << el.get() <<", ";
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
    std::vector< std::unordered_multiset< T > > neighbors;
    std::vector< T > subRegionElements;
    bool contiguous;

    bool moveToNeighbor = false;
    std::vector< size_t > advanceDirections; 
    std::string name;

    TestCase_MatrixGraphTraverser( 
            Test_MatrixGraphTraverser_DataVector< T >& _refData, bool _contiguous, 
            std::string&& _name = "", NG _neighGetter = defaultMatrixNeighborGetter 
    )
     : refData( _refData ), neighGetter( _neighGetter ), contiguous( _contiguous ),
       name( std::move( _name ) )
    {}
};


// Tests advancement
template< typename T >
class Test_MatrixGraphTraverser{
private:
    TestCase_MatrixGraphTraverser< T > testCase;
    MatrixGraphTraverser< T > trav;

    static MatrixGraphTraverser<T> createTraverser( 
            const TestCase_MatrixGraphTraverser<T>& testCase)
    {
        return MatrixGraphTraverser< T >( 
            testCase.refData.data, 
            std::vector<size_t>( testCase.refData.dimensions ), 
            std::vector<size_t>( testCase.startCoords ),
            std::vector<size_t>( testCase.offset ), 
            std::vector<size_t>( testCase.endOffset ), 
            testCase.useCoordChecking,
            testCase.neighGetter
        );
    }

public:
    // Copy 'n' Move Constructors.
    Test_MatrixGraphTraverser( TestCase_MatrixGraphTraverser< T >&& tcase )
     : testCase( std::move( tcase ) ), trav( createTraverser( testCase ) )
    {}

    Test_MatrixGraphTraverser( const TestCase_MatrixGraphTraverser< T >& tcase )
     : testCase( tcase ), trav( createTraverser( testCase ) )
    {}
    
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
                {
                    // Check neighbor element references.
                    auto&& neighVec = trav.getNeighborElements();
                    auto&& mset = std::unordered_multiset< T >( neighVec.begin(), neighVec.end() );
                    assert( testCase.neighbors[ i ] == mset );
                }
                {
                    // Check neighbor traversers.
                    auto&& neighTravs = trav.getNeighborTraversers();
                    std::unordered_multiset< T > travSet;
                    for( auto&& a : neighTravs ){
                        travSet.insert( a.getValue() );
                    }
                    assert( testCase.neighbors[ i ] == travSet );
                }
                {
                    // Check base traversers.
                    auto&& baseTravs = trav.getNeighborBaseTraversers();
                    std::unordered_multiset< T > baseTravSet;
                    for( auto&& a : baseTravs ){
                        baseTravSet.insert( a->getValue() );
                    }
                    assert( testCase.neighbors[ i ] == baseTravSet ); 
                }
            }

            // Data and index must be compliant.
            assert( trav.getValue() == regionExpected[ i ] );
            if( testCase.contiguous )
                assert( trav.getIndex() == i );

            // Make advancement according to test case's properties.
            if( testCase.moveToNeighbor && i < testCase.advanceDirections.size() ){
                trav.moveToNeighbor( testCase.advanceDirections[ i ] );
            }
            else{
                size_t direc = ( i < testCase.advanceDirections.size() ? 
                                     testCase.advanceDirections[ i ] : 0 );
                // If not last, advance must be successful
                if( i < regionExpected.size() - 1 )
                    assert( trav.advance( direc ) );
                // If at the last element, advance must return false.
                else
                    assert( !trav.advance( direc ) );
            }
        }
    }

    void full( int verbose = 0 ){
        advance( verbose );
    }
};

// Tests the Traverser.
void testTraverser(){
    int verbosity = 1;

    std::vector< Test_MatrixGraphTraverser_DataVector< std::string > > tDatas( {
        // 2D grid
        Test_MatrixGraphTraverser_DataVector< std::string >( {
            "1",  "2",  "3",  "4",  "5", 
            "6",  "7",  "8",  "9",  "10", 
            "11", "12", "13", "14", "15",
            "16", "17", "18", "19", "20"
        }, { 5, 4 } ),
         
        // Test 3D case - Rubik's Cube.
        Test_MatrixGraphTraverser_DataVector< std::string >( {
            "312", "21", "214",
            "31",   "1",  "14",
            "315", "51", "514",

            "32",  "2",  "24",
            "3",   "C",   "4",
            "35",  "5",  "54",

            "362", "26", "264",
            "36",   "6",  "64",
            "365", "56", "564", 
        }, { 3, 3, 3 } )
    } );

    // Test cases to be computed.
    // 2D cases
    std::vector< TestCase_MatrixGraphTraverser<std::string> > testCases( {
        TestCase_MatrixGraphTraverser<std::string>( tDatas[0], true, "Contiguous 2D" ),
    } );

    {
    TestCase_MatrixGraphTraverser<std::string> tmp(tDatas[0], false,"Mid-Region 2D");
    tmp.startCoords = { 1, 1 };
    tmp.offset = { 1, 1 };
    tmp.endOffset = { 3, 3 };
    tmp.subRegionElements = { "7", "8", "9", "12", "13", "14", "17", "18", "19" };
    tmp.neighbors = { {"12","8"}, {"7","13","9"}, {"8","14"}, {"7", "13", "17"}, 
        {"12", "8", "14", "18"}, {"9", "13", "19"}, {"12", "18"}, {"17", "19", "13"}, 
        {"18", "14" } };

    testCases.push_back( std::move( tmp ) );
    }
    {
    TestCase_MatrixGraphTraverser<std::string> tmp(tDatas[0], false,
                                    "Mid-Region 2D, Move on Y-axis.");
    tmp.startCoords = { 1, 1 };
    tmp.offset = { 1, 1 };
    tmp.endOffset = { 3, 3 };
    tmp.subRegionElements = { "7", "12", "17", "8", "13", "18", "9", "14", "19" };
    tmp.advanceDirections = std::vector<size_t>( tmp.subRegionElements.size(), 1 ); 

    testCases.push_back( std::move( tmp ) );
    }
    {
    TestCase_MatrixGraphTraverser<std::string> tmp(tDatas[0], false,
                                    "Mid-Region 2D, Move to East-South Neighbors.");
    tmp.startCoords = { 1, 1 };
    tmp.offset = { 1, 1 };
    tmp.endOffset = { 3, 3 };
    tmp.subRegionElements = { "7", "12", "17", "18", "13", "8", "9", "14", "19" };
    tmp.moveToNeighbor = true;
    tmp.advanceDirections = { 1, 1, 0, 2, 3, 0, 1, 1 };

    testCases.push_back( std::move( tmp ) );
    } 
    
    // 3D cases
    testCases.push_back( TestCase_MatrixGraphTraverser<std::string>( tDatas[1], true, 
                         "Contiguous 3D Rubik's Cube" ) );
    {
    TestCase_MatrixGraphTraverser<std::string> tmp( tDatas[1], false, "Mid-Region 3D Cube");
    tmp.startCoords = { 1, 1, 1 };
    tmp.offset = { 1, 1, 1 };
    tmp.endOffset = { 2, 2, 2 };
    tmp.subRegionElements = { "C", "4", "5", "54", 
                              "6", "64", "56", "564" };
    tmp.neighbors = { {"4","5","6"}, {"C","54","64"}, {"C","54","56"}, {"5","4","564"},
                      {"56","64","C"}, {"6","564","4"}, {"6","564","5"}, {"64","56","54"} };
    
    testCases.push_back( std::move( tmp ) );
    }

    // Launch all tests.
    for( size_t i = 0; i < testCases.size(); i++ ){
        if( verbosity > 0 )
            std::cout << "\n=====================\nTest #"<< i <<": "<< 
                         testCases[i].name << "\n";

        auto tester = Test_MatrixGraphTraverser<std::string>( std::move( testCases[i] ) );
        tester.full( verbosity - 1 );
    }
}

}

