#ifndef BARRIER_MESSAGE_SERVICE_HPP_INCLUDED
#define BARRIER_MESSAGE_SERVICE_HPP_INCLUDED

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

/*! Barrier-specific ThreadState
 */ 
struct BarrierThreadState : public ThreadState{
    using ThreadState::ThreadState;
    volatile bool pollAvailable = true;
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
class BarrierMessageService : public MessageService<MessType>{
private:
    std::vector< MessType > ringBuffer;
    size_t readPos = 0;
    size_t writePos = 0;
    size_t itemCount = 0;

    volatile bool dispatcherClosed = false;
    volatile bool resetNeeded = false;
    int verb = 0;

    std::mutex mut;
    std::condition_variable cond;

    std::set< std::shared_ptr<BarrierThreadState>, decltype(compareLambda) > receiverThreads;
    size_t pollableThreads = 0;

    /*! These private functions assume lock is already acquired.
     * - Returns an iterator to std::shared_ptr to threadState object with id = id.
     *  @param id - Thread's ID.
     */ 
    auto findReceiverThread( std::thread::id id ){
        return receiverThreads.find( std::make_shared< BarrierThreadState >( id ) );
    }

    /*! ======== Synchronized Barrier Service functions. ==========  
     *
     * Checks available polls, and resets poll counter if needed. 
     *  - If all threads have already polled current message, reset poll 
     *    availability, and move on to the next message.
     */ 
    size_t checkResetPolls(){
        if( !pollableThreads ){
            for( auto&& th : receiverThreads ){
                th->pollAvailable = true;
            }
            pollableThreads = receiverThreads.size();

            if( itemCount > 0 ){
                readPos++;
                itemCount--;  
            }

            Util::vlog(2, verb, "\nThis was the last thread which received the message! "\
                 "Resetting counters, procceeding with the next message.\n");

            cond.notify_all();
        } 
        return pollableThreads;
    }

    void setPollAvailable( std::shared_ptr< BarrierThreadState > ts, bool val ){
        if( (ts->pollAvailable) && !val )
            pollableThreads--;
        else if( !(ts->pollAvailable) && val )
            pollableThreads++;     

        ts->pollAvailable = val;
    }

public:
    const static size_t DEFAULT_BUFFSIZE = 256;
    const static size_t DEFAULT_VERBOSITY = 0;

    /*! Constructor. 
     *  - Creates initial buffer.
     */  
    BarrierMessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
                    std::initializer_list<MessType>&& initialMess = {},
                    int _verbosity = DEFAULT_VERBOSITY )
    : ringBuffer( initialMess ), writePos( initialMess.size() ), 
      itemCount( initialMess.size() ), verb( _verbosity ),
      receiverThreads( compareLambda )
    {
        if( ringBuffer.size() < buffSize ){
            ringBuffer.reserve( buffSize );
            for( size_t i = 0; i < (buffSize - ringBuffer.size()); i++ ){
                ringBuffer.push_back( MessType() );
            }
        }
    }

    BarrierMessageService( const BarrierMessageService& ) = delete;
    BarrierMessageService& operator=( const BarrierMessageService& ) = delete;

    /*! Dispatches new message - pushes it to the ring buffer queue.
     *  - Notifies all waiters to check whether they can accept the message.
     */ 
    void dispatchMessage( MessType&& mess ){
        // Lock the critical section - adding a new message to queue.
        {
            std::lock_guard< std::mutex > lock( mut );
            if( writePos >= ringBuffer.size() )
                writePos = 0;

            // Move data to the position in buffer.
            ringBuffer[ writePos ] = std::move( mess );

            writePos++;
            itemCount++;

            Util::vlog( 2, verb, "[dispatchMsg]: (%p), writePos: %d, readPos: %d, itemCount: %d\n",
                  &ringBuffer[ writePos - 1 ], writePos, readPos, itemCount );
        }
        // Wake up waiting threads to procceed with message consuming.
        cond.notify_all(); 
    }

    bool pollMessage( MessType& mess ){
        std::unique_lock< std::mutex > lock( mut );

        // Check for "death situation".
        if( dispatcherClosed && !itemCount ){
            Util::vlog( 1, verb, " [pollMsg]: Dispatcher is Closed! No more messages to expect!\n" );
            return false;
        }

        // Get the calling thread's state structure.
        auto&& threadID = std::this_thread::get_id();
        auto&& tIter = findReceiverThread( threadID );

        if( tIter == receiverThreads.end() ){
            Util::vlog( 0, verb, "[pollMsg]: Thread %s hasn't subscribed!\n",
                  Util::toString(threadID).c_str() );
            return false;
        }

        std::shared_ptr< BarrierThreadState > threadState = *tIter;

        // Check for wait conditions. 
        // If thread has already received the message (or no available), 
        // wait until new message appears - new poll becomes available.
        while( ( !(threadState->pollAvailable) || !itemCount ) && 
               threadState->isSubscribed && !(dispatcherClosed && !itemCount) )
        {
            cond.wait( lock );
        }
        
        // This thread won't be able to poll no more in this round.
        // So set this thread's poll availability to false.
        setPollAvailable( threadState, false );

        if( readPos >= ringBuffer.size() )
            readPos = 0;

        mess = ringBuffer[ readPos ]; 

        Util::vlog(2, verb," [pollMsg] (%p): Thread %s RECEIVED this the first time. \n" \
                     "  writePos: %d, readPos: %d, itemCount: %d\n", 
            &ringBuffer[readPos], Util::toString(threadID).c_str(), 
            writePos, readPos, itemCount ); 

        // Check if all receivers have polled the current message, and if so,
        // reset the poll counters, and make threads poll again.
        checkResetPolls();

        return true;
    } 

    /*! "Subscribe" to message service. 
     *  - Adds the calling thread's ID to the Receiver Threads lists.
     */ 
    bool subscribe(){
        std::lock_guard< std::mutex > lock( mut );

        // Create new Thread State, and increment pollable thread counter.
        auto thState = std::make_shared<BarrierThreadState>( std::this_thread::get_id() );
        auto res = receiverThreads.insert( thState );

        // insert() returns std::pair, with second element indicating if new 
        // element was inserted to a set.
        if( res.second ){
            thState->pollAvailable = true;
            pollableThreads++;

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
            setPollAvailable( *iter, false );

            // Mark thread as no longer subscribed, and erase from the list.
            (*iter)->isSubscribed = false;
            receiverThreads.erase( iter );
        }
    }

    /*! Marks dispatcher as "closed", hencefore no more message polls will be accepted.
     */ 
    void closeDispatcher(){
        dispatcherClosed = true;

        Util::vlog( 2, verb, "\nDispatcher is closing!\n" );

        // To prevent DeadLocks, notify the waiters to check the condition.
        cond.notify_all();
    }

     
};

#endif // BARRIER_MESSAGE_SERVICE_HPP_INCLUDED

