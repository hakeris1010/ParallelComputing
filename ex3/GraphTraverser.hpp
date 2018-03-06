#ifndef GRAPH_TRAVERSER_HPP_INCLUDED
#define GRAPH_TRAVERSER_HPP_INCLUDED

namespace gtools{

/*! A Cross-Interface for a Graph Traversing utility implementations.
 *  - Genericizes features such as current element getting, neighbor element getting,
 *    advancing to a specified neighbor or direction.
 *  - Enforces const-correctness only to a traverser structure itself:
 *    the Elements traverser points to are not considered constant even if the traverser 
 *    is constant, though.
 */
template< typename Element >
class GraphTraverser{
public:
    virtual Element& getValue() const = 0;
    virtual std::vector< std::reference_wrapper< Element > > getNeighborElements() const = 0;
    virtual std::vector< std::shared_ptr< GraphTraverser<Element> > > 
            getNeighborBaseTraversers() const = 0;

    virtual bool advance( const size_t direction ) = 0;
    virtual void moveToNeighbor( const size_t neighborIndex ) = 0;
};

}

#endif // GRAPH_TRAVERSER_HPP_INCLUDED 


