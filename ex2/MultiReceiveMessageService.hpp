#ifndef MULTIRECEIVE_MESSAGE_SERVICE_HPP_INCLUDED
#define MULTIRECEIVE_MESSAGE_SERVICE_HPP_INCLUDED

#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <sstream>
#include <functional>
#include "MessageService.hpp"

/*! Multi-receiver async reading-ready state.
 *  - Includes a position in an input sequence.
 */ 
struct MultiReceiverThreadState : public ThreadState {
    using ThreadState::ThreadState;
    size_t currPos;
};

/*! TODO (NO LONGER): 
 * - CONFIRMED that this approach is not optimal because of limited usability and
 *   high interconnection to individual features of each component.
 *
 * Pollability Control & Configuration classes.
 * - Checks and controls message receiver's ability to poll new messages.
 * - pollAvailable( id ) indicate whether this ID is able to poll a new message,
 *   must wait, or is no longer allowed to poll at all.  
 */ 
template<class MessType>
class MultiReceiverMessageService : public MessageService<MessType>{
private:
    // Option control properties.
    const int verb;
    const bool overwriteOldest;

    // Message buffer and properties.
    std::vector< MessType > ringBuffer;

    // Available region end and start positions (writePos and lastElemPos)
    size_t writePos = 0;
    size_t lastElemPos = 0;

    // Indicates whether available region is splitted, i.e. latest elements are 
    // at the beginning, however there are still old elements at the end.
    bool regionSplitted = false;

    // Size o' available region.
    size_t itemCount = 0;

    // "Dispacther is dead" property.
    volatile bool dispatcherClosed = false;

    // Container for receiver thread states (position in buffer, etc.).
    std::set< std::shared_ptr<MultiReceiverThreadState>, decltype(compareLambda) > receiverThreads;

    // How many threads haven't yet read the oldest element.
    size_t lastElemRemainingReaders = 0;

    // Synchronizing variables. Protecc's several stuff.
    // Protects the message buffer and properties which are written at every call.
    std::mutex mut;

    // Condvar on which receiver thread waits when there are no more elements to read.
    // Notified when new element is added to buffer.
    std::condition_variable cond_newElemAdded;

    // Condvar on which dispatcher waits for receivers to read the oldest element 
    // when no place is left in the buffer. Notified when oldest elem gets removed.
    std::condition_variable cond_oldestElemRemoved;

    /*! 
     *  Private functions assume that LOCK is ALREADY ACQUIRED.
     *
     * - Returns an iterator to std::shared_ptr to threadState object with id = id.
     *  @param id - Thread's ID.
     */ 
    auto findReceiverThread( std::thread::id id ){
        return receiverThreads.find( std::make_shared< MultiReceiverThreadState >( id ) );
    }

    /*! Removes the oldest element from buffer, performing these jobs:
     *  - Checks for threads which haven't yet read the oldest element, and
     *    if there are such threads, then iterates through threadStates and moves
     *    the currPos forward for threads which are still on oldest element position.
     *  - At the same time, checks for threads which are on the next elem after the last, 
     *    incrementing the lastElemRemainingReaders accordingly.
     *  - After that's done, increments the lastElemPos counter.
     */ 
    void removeOldestMessage(){
        // Compute new lastElemRemainingReaders value, fixing thread states at the same time.
        lastElemRemainingReaders = 0;
        for( auto&& state : receiverThreads ){
            // Position is on the lastElem - move forward, to the new lastElem.
            if( state->currPos == lastElemPos ){
                state->currPos = ( state->currPos + 1 ) % ringBuffer.size();
                lastElemRemainingReaders++;
            }
            // Position on the next after lastElem - it's the new lastElem.
            else if( state->currPos == lastElemPos+1 ){
                lastElemRemainingReaders++;
            }
        }
        // New value of last elem readers is computed, now just increment the lastElemPos.
        lastElemPos = ( lastElemPos + 1 ) % ringBuffer.size();
    }

public:
    const static size_t DEFAULT_BUFFSIZE = 256;
    const static size_t DEFAULT_VERBOSITY = 0;

    /*! Constructor. 
     *  - Creates initial buffer.
     */  
    MultiReceiverMessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
                    bool _overwriteOldest = true,
                    std::initializer_list<MessType>&& initialMess = {},
                    int _verbosity = DEFAULT_VERBOSITY )
    : verb( _verbosity ), overwriteOldest( _overwriteOldest ),
      ringBuffer( initialMess ), writePos( initialMess.size() ), 
      itemCount( initialMess.size() ), 
      receiverThreads( compareLambda )
    {
        if( ringBuffer.size() < buffSize ){
            ringBuffer.reserve( buffSize );
            for( size_t i = 0; i < (buffSize - ringBuffer.size()); i++ ){
                ringBuffer.push_back( MessType() );
            }
        }
    }

    MultiReceiverMessageService( const MultiReceiverMessageService& ) = delete;
    MultiReceiverMessageService& operator=( const MultiReceiverMessageService& ) = delete;

    /*! Dispatches new message - pushes it to the ring buffer queue.
     *  - Notifies all waiters to check whether they can accept the message.
     */ 
    void dispatchMessage( MessType&& mess ){
        // Lock the commonly-written Class and Buffer mutex - adding a new message to queue.
        {
            std::unique_lock< std::mutex > lock( mut );

            // Condition when there's no place to write to - the write pos has reached 
            // the last elem pos.
            if( writePos == lastElemPos ){
                if( overwriteOldest ){
                    // Remove oldest (moving receiver pointers to the next one).
                    removeOldestMessage();
                }
                else{ 
                    // Wait until the oldest one gets removed (read by all receiv0rs).
                    while( writePos == lastElemPos ){
                        cond_oldestElemRemoved.wait( lock );
                    }
                }
            }
            // At this point write position is OK, because this function is the
            // only one which is modifying the write position.

            // MOVE data to the position in buffer.
            ringBuffer[ writePos ] = std::move( mess );

            // Increment write position (end of region) modularly.
            writePos = (writePos + 1) % ringBuffer.size();
            itemCount++;

            Util::vlog( 2, verb, "[dispatchMsg SUCC!]: (%p), writePos: %d, itemCount: %d\n",
                  &ringBuffer[ writePos - 1 ], writePos, itemCount );
        }
        // Notify waiting receivers that new element has been added.
        cond_newElemAdded.notify_all();
    }

    bool pollMessage( MessType& mess ){
        std::unique_lock< std::mutex > lock( mut );

        // Check for death situations.
        if( dispatcherClosed && !itemCount ){
            _dispatcherClosed:
            Util::vlog( 1, verb, " [pollMsg]: Dispatcher is Closed! No more messages to expect!\n" );
            return false;
        }

        // Get the state of calling thread.
        auto&& threadID = std::this_thread::get_id();
        auto&& tIter = findReceiverThread( threadID );

        if( tIter == receiverThreads.end() ){
            _notSubscribed: 
            Util::vlog( 0, verb, "[pollMsg]: Thread %s hasn't subscribed!\n",
                        Util::toString(threadID).c_str() );
            return false;
        }

        std::shared_ptr< MultiReceiverThreadState > threadState = *tIter;

        // Wait if no elements can be read.
        // Our position (currPos) is always in these states:
        //  - ALWAYS in the space higher or equal than the available region beginning,
        //    so we don't have to check if it's before the beginning of the region.
        //  - However, it can be equal to region's end ( writePos ), if the writer hasn't yet
        //    added a new message and we extracted all of them.  
        //  SO:
        //  - We only have to check for one condition: if it's EQUAL to writePos.
        //  - If it's NOT equal to WritePos, it's in the available region.
        //
        while( !itemCount || ( threadState->currPos == writePos ) ) {
            // Check for the specific end conditions.
            if( !threadState->isSubscribed )
                goto _notSubscribed;
            else if( dispatcherClosed && !itemCount )
                goto _dispatcherClosed;

            // Wait until new element gets added.
            cond_newElemAdded.wait( lock );
        }
        // At this point, the currPos in the available region and can process a message.
        // We don't need to fix overflows because we always increment positions modularly.
       
        // Copy message to memory passed. Can't move because there are many readers which
        // could also read this message.
        mess = ringBuffer[ threadState->currPos ]; 

        // Check if it's the oldest message, and perform jobs if so.
        if( threadState->currPos == lastElemPos ){
            if( lastElemRemainingReaders > 0 )
                lastElemRemainingReaders--;
            
            // If we were the last ones to read it, remove it.
            if( !lastElemRemainingReaders ){
                removeOldestMessage();
                //lastElemPos = ( lastElemPos + 1 ) % ringBuffer.size();
            }
        }

        // And increment the current position, moving on to next element.
        threadState->currPos = (threadState->currPos + 1) % ringBuffer.size(); 

        // Success!
        Util::vlog( 2, verb, " [pollMsg] (%p): Thread %s RECEIVED SUCCESSFULLY. \n" \
                             "  writePos: %d, currPos: %d, itemCount: %d\n", 
                    &ringBuffer[readPos], Util::toString(threadID).c_str(), 
                    writePos, threadState->currPos, itemCount ); 

        return true;
    } 

    /*! "Subscribe" to message service. 
     *  - Adds the calling thread's ID to the Receiver Threads lists.
     */ 
    bool subscribe(){
        std::lock_guard< std::mutex > lock( mut );

        // Create new Thread State, and increment pollable thread counter.
        auto thState = std::make_shared<MultiReceiverThreadState>( std::this_thread::get_id() );
        auto res = receiverThreads.insert( thState );

        // insert() returns std::pair, with second element indicating if new 
        // element was inserted to a set.
        if( res.second ){
            thState->currPos = lastElemPos;
            lastElemRemainingReaders++;
        }
    }

    /*! "UnSubscribe" from the message service. 
     *  - Removes the calling thread's ID from the Receiver Threads lists.
     */ 
    void unSubscribe(){
        std::lock_guard< std::mutex > lock( mut );

        auto&& iter = findReceiverThread( std::this_thread::get_id() );
        if( iter != receiverThreads.end() ){
            // Decrement the "threads on last element" counter if this thread was one of them.
            if( (*iter)->currPos == lastElemPos && lastElemRemainingReaders > 0 )
                lastElemRemainingReaders--;

            // Mark thread as no longer subscribed, and erase from the list.
            (*iter)->isSubscribed = false;
            receiverThreads.erase( iter );
        }
    }

    /*! Marks dispatcher as "closed", hencefore no more message polls will be accepted.
     */ 
    void closeDispatcher(){
        {
            std::lock_guard< std::mutex > lock( mut );
            dispatcherClosed = true;
        }
        Util::vlog( 2, verb, "\nDispatcher is closing!\n" );

        // To prevent DeadLocks, notify the waiters to check the dispatcherClosed condition.
        cond_newElemAdded.notify_all();
    }

};

#endif // MULTIRECEIVE_MESSAGE_SERVICE_HPP_INCLUDED

