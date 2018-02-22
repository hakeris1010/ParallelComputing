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

/*! Multi-receiver async reading-ready state. Includes:
 *  - A position in an input message sequence.
 *  - Count of all messages received by this thread.
 *    This is used for checking whether a thread has reached the end of sequence
 *    and mush wait for new message, or can procceed.
 */ 
struct MultiReceiverThreadState : public ThreadState {
    using ThreadState::ThreadState;
    size_t currPos = 0;
    size_t receiveCount = 0;
    //size_t roundCount = 0;
};

/*! Multiple Receiver Async-get Message service class.
 *  - Provides capabilities for multiple dispatchers (acting as one through sync),
 *    and multiple receivers, allowing them to Asynchronously process incoming messages.
 *  - Every receiver must first "Subscribe" to a service, thus an associated 
 *    ThreadState structure is created for the subscribing receiver.
 *  - ThreadState stores position in input sequence and other data.
 *
 *  - The receiver threads can process messages from a buffer until no more messages 
 *    are left, and then the next call blocks until new message arrives.
 *  
 *  - Service uses Ring-Buffer for storing messages.
 *  - Oldest message is being removed from buffer when:
 *    a) All receiver threads have processed that message
 *    b) No space is left in the buffer. In this case there are 2 options:
 *      1. Oldest message is removed forcefully, moving all receivers which 
 *         still haven't read it to the next message.
 *      2. Dispatcher waits until the oldest message is processed by all receivers.
 *       This behavior is controlled by "overwriteOldest" switch.
 *
 *  - Constructor allows:
 *    - initial buffer filling with messages to be processed first
 *    - setting buffer size
 *    - setting "overwriteOldest" switch
 *    - custom verbose outputs for debug purposes.
 */  
template<class MessType>
class MultiReceiverMessageService : public MessageService<MessType>{
private:
    const static size_t DEFAULT_BUFFSIZE  = 128;
    const static int    DEFAULT_VERBOSITY = 0;
    const static bool   DEFAULT_OVERWRITE_OLDEST = false;

    // Option control properties.
    const int verb;
    const bool overwriteOldest;

    // Message buffer and properties.
    //std::vector< BufferElement< MessType > > ringBuffer;
    std::vector< MessType > ringBuffer;

    // Available region end and start positions (writePos and lastElemPos)
    size_t writePos = 0;
    size_t lastElemPos = 0;

    // Counts how many rounds the lastElemPos has hit the position 0 - 
    // How many full writes have occured.
    size_t roundCount = 0;

    // Indicates whether available region is splitted, i.e. latest elements are 
    // at the beginning, however there are still old elements at the end.
    bool regionSplitted = false;

    // Size o' available region.
    size_t itemCount = 0;

    // How many items (messages) were dispatched overall
    size_t dispatchedMessageCount = 0;

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
     *  - Iterates through threadStates and increments currPos'es pointing to lastElemPos.
     *  - At the same time, checks for threads which are on the next elem after the last, 
     *    incrementing the lastElemRemainingReaders accordingly.
     *  - After that's done, increments the lastElemPos counter.
     *
     *  - If there are no elements (itemCount == 0), does nothing.
     *  - Even if lastElemPos == writePos, it's nothing bad, because it just means buffer
     *    is full.
     */ 
    void removeOldestMessage(){
        if( !itemCount )
            return;
 
        Util::vlog( 1, verb, "[removeOldestMsg OLD]: remRdrs: %d, " \
                    "lastElPos: %d, writePos: %d, dispatchCnt: %d\n",
            lastElemRemainingReaders, lastElemPos, writePos, dispatchedMessageCount );
        
        // Compute new lastElemRemainingReaders value, fixing thread states at the same time.
        lastElemRemainingReaders = 0;
        for( auto&& state : receiverThreads ){
            // Position is on the lastElem - move forward, to the new lastElem.
            if( state->currPos == lastElemPos && 
                state->receiveCount != dispatchedMessageCount )
            {
                state->currPos = ( state->currPos + 1 ) % ringBuffer.size();
                state->receiveCount++;
                lastElemRemainingReaders++;

                Util::vlog( 1, verb, "[removeOldestMsg]: Incrementing currPos of thread %s.\n",
                            Util::toString( state->id ).c_str() );
            }
            // Position on the next after lastElem - it's the new lastElem.
            else if( state->currPos == ((lastElemPos+1) % ringBuffer.size()) ){
                lastElemRemainingReaders++;
            }
        }
        // New value of last elem readers is computed, now just increment the
        // position of the last element, and remove the oldest item.
        lastElemPos = ( lastElemPos + 1 ) % ringBuffer.size();
        itemCount--;

        Util::vlog( 2, verb, "[removeOldestMsg NEW]: remRdrs: %d, " \
                    "lastElPos: %d, writePos: %d, dispatchCnt: %d\n\n",
            lastElemRemainingReaders, lastElemPos, writePos, dispatchedMessageCount );

        // Notify about removal.
        cond_oldestElemRemoved.notify_all();
    }

public:
    /*! Constructor. 
     *  - Creates initial buffer.
     */  
    MultiReceiverMessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
                    bool _overwriteOldest        = DEFAULT_OVERWRITE_OLDEST,
                    int _verbosity               = DEFAULT_VERBOSITY,
                    std::initializer_list<MessType>&& initialMess = {} )
    : verb( _verbosity ), overwriteOldest( _overwriteOldest ),
      ringBuffer( initialMess ), itemCount( initialMess.size() ), 
      dispatchedMessageCount( initialMess.size() ),
      receiverThreads( compareLambda )
    {
        writePos = 0;
        if( ringBuffer.empty() ){
            ringBuffer.assign( buffSize, MessType() );
        }
        else if( ringBuffer.size() < buffSize ){
            writePos = ringBuffer.size() % buffSize;
            ringBuffer.reserve( buffSize );

            // Fill the remaining place with empty elements.
            for( size_t i = ringBuffer.size(); i < buffSize; i++ ){
                ringBuffer.push_back( MessType() );
            }
        }

        Util::vlog( 1, verb, "[MultiReceiverMessageService constructor]: BuffSize: %d\n",
                    ringBuffer.size() );
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

            // If closed dispatcher has been marked, it can no longer post messages.
            if( dispatcherClosed )
                return;

            // Condition when there's no place to write to - the write pos has reached 
            // the last elem pos.
            if( itemCount && writePos == lastElemPos ){
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
            dispatchedMessageCount++;

            // Increment the round count if lastElemPos once again reached buffer start.
            if( writePos == 0 )
                roundCount++; 

            Util::vlog( 2, verb, "[dispatchMsg SUCC!]: (0x%p), writePos: %d, " \
                        "itemCount: %d, dispatchCount:%d\n", &ringBuffer[ writePos - 1 ], 
                        writePos, itemCount, dispatchedMessageCount);
        }
        // Notify waiting receivers that new element has been added.
        cond_newElemAdded.notify_all();
    }

    /*! Called by receiver threads, polls a message from buffer, or waits if there are
     *  no more ahead of thread's state's position in a buffer.
     */ 
    bool pollMessage( MessType& mess ){
        std::unique_lock< std::mutex > lock( mut );

        // Get the state of calling thread.
        auto&& threadID = std::this_thread::get_id();
        auto&& tIter = findReceiverThread( threadID );
        
        if( tIter == receiverThreads.end() ){
            _notSubscribed: 
            Util::vlog( 1, verb, "[pollMsg]: Thread %s hasn't subscribed!\n",
                        Util::toString(threadID).c_str() );
            return false;
        }

        std::shared_ptr< MultiReceiverThreadState > threadState = *tIter;

        // Polling starts!
        Util::vlog( 2, verb, "[pollMsg]: Thread %s is polling. dispatchCnt: %d, " \
                    "threadState->receiveCnt: %d \n", Util::toString(threadID).c_str(), 
                    dispatchedMessageCount, threadState->receiveCount );

        if( verb >= 1 && threadState->currPos == lastElemPos && lastElemPos == writePos &&
            itemCount == ringBuffer.size() )
        {
            // If so, ignore the wait.
            Util::vlog( 1, verb, "[pollMsg]: (Thread %s): BUFFER FULL! dispatchCnt: %d, " \
                        "threadState->receiveCnt: %d \n", Util::toString( threadID ).c_str(),
                        dispatchedMessageCount, threadState->receiveCount );
        }

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
        //  Conditions of each line: 
        //  1. We're on the end of buffer, no messages which were dispatched and not
        //     yet received by this thread.
        //  2. Situation is pollable (thread still subscribed and dispatcher still working).
        //
        while( ( threadState->receiveCount == dispatchedMessageCount ) && 
               ( threadState->isSubscribed && !dispatcherClosed ) ) 
        {
            cond_newElemAdded.wait( lock ); // Wait until new element gets added.
        }

        // Check for the specific end conditions.
        // Dispatcher already closed, and this thread has already received all messages.
        if( dispatcherClosed && threadState->receiveCount == dispatchedMessageCount )
        {
            Util::vlog( 1, verb, " [pollMsg]: (Thread %s): Dispatcher is CLOSED! (" \
                "itc: %d, wrt: %d, lst: %d, crp: %d\n", Util::toString(threadID).c_str(),
                itemCount, writePos, lastElemPos, threadState->currPos );
            return false;
        }
        if( !threadState->isSubscribed )
            goto _notSubscribed;

        // At this point, the currPos in the available region and can process a message.
        // We don't need to fix overflows because we always increment positions modularly.
       
        // Copy message to memory passed. No move semantics because others could read it too.
        mess = ringBuffer[ threadState->currPos ]; 

        // Increment the current position and received msg counter, moving on to next element.
        size_t oldCurpos = threadState->currPos;
        threadState->currPos = (threadState->currPos + 1) % ringBuffer.size();  

        threadState->receiveCount++;

        // Synchronize RoundCount, after successfully extracted a message in current round.
        if( threadState->currPos == 0)
            threadState->roundCount = roundCount; 

        Util::vlog( 2, verb, "[pollMsg SUCC!]: Thread %s RECEIVED message \"%s\".\n" \
                             "                 writePos: %d, currPos: %d, itemCount: %d\n", 
                    Util::toString(threadID).c_str(), Util::toString( mess ).c_str(),
                    writePos, threadState->currPos, itemCount ); 

        // Check if we've just read an oldest message, and perform jobs if so.
        if( oldCurpos == lastElemPos )
        {
            if( lastElemRemainingReaders > 1 )
                lastElemRemainingReaders--;
            else{
                // If we were the last ones to read it, remove the last element.
                removeOldestMessage();
            }
        }

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
            // Set position in buffer and current buffer's write round count. 
            thState->currPos = lastElemPos;
            thState->roundCount = roundCount;

            lastElemRemainingReaders++;

            Util::vlog( 2, verb, "[subscribe()]: Thread %s subscribed successfully!\n",
                        Util::toString( std::this_thread::get_id() ).c_str() );
            return true;
        }
        return false;
    }

    /*! "UnSubscribe" from the message service. 
     *  - Removes the calling thread's ID from the Receiver Threads lists.
     */ 
    void unSubscribe(){
        std::lock_guard< std::mutex > lock( mut );

        auto&& iter = findReceiverThread( std::this_thread::get_id() );
        if( iter != receiverThreads.end() ){
            // Decrement the "threads on last element" counter if this thread was one of them.
            if( (*iter)->currPos == lastElemPos && lastElemRemainingReaders > 0 &&
                (*iter)->receiveCount != dispatchedMessageCount )
                lastElemRemainingReaders--;

            // Mark thread as no longer subscribed, and erase from the list.
            (*iter)->isSubscribed = false;
            receiverThreads.erase( iter );

            Util::vlog( 2, verb, "[unSubscribe()]: Thread %s UnSubscribed successfully.\n",
                        Util::toString( std::this_thread::get_id() ).c_str() ); 
        }
    }

    /*! Marks dispatcher as "closed", hencefore no more message polls will be accepted.
     */ 
    void closeDispatcher(){
        {
            std::lock_guard< std::mutex > lock( mut );

            dispatcherClosed = true;
            Util::vlog( 2, verb, "\nDispatcher is closing!\n" );
        }

        // To prevent DeadLocks, notify the waiters to check the dispatcherClosed condition.
        cond_newElemAdded.notify_all();
    }

};

#endif // MULTIRECEIVE_MESSAGE_SERVICE_HPP_INCLUDED

