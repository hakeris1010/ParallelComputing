#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <set>
#include <memory>
#include <sstream>

template< typename... Args >
void vlog( const int threshold, int verbosity, const char* str, Args&&... args ){
    if( verbosity >= threshold ){
        printf( str, std::forward<Args>(args)... );
        fflush( stdout );
    }
}

std::string toString( const auto& a ){
    std::stringstream str;
    str << a;
    return str.str();
}

struct ReceiverThreadState{
    std::thread::id id;
    volatile bool pollAvailable = false;
    volatile bool isSubscribed = true;

    bool operator< (const ReceiverThreadState& other) const {
        return id < other.id;
    }
    bool operator== (const ReceiverThreadState& other) const {
        return id == other.id;
    } 

    ReceiverThreadState( std::thread::id _id ) : id( _id ) {}
};

template<class MessType>
class MessageService{
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

    std::set< std::shared_ptr<ReceiverThreadState>, 
              bool( const std::shared_ptr<ReceiverThreadState>&,
                    const std::shared_ptr<ReceiverThreadState>& )
            > receiverThreads;

    size_t pollableThreads = 0;

    /*! These private functions assume lock is already acquired.
     */ 
    auto&& findReceiverThread( std::thread::id id ){
        return receiverThreads.find( std::make_shared< ReceiverThreadState >( id ) );
    }

    /*! Checks available polls, and resets poll counter if needed. 
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

            vlog(2, verb, "\nThis was the last thread which received the message! "\
                 "Resetting counters, procceeding with the next message.\n");

            cond.notify_all();
        } 
        return pollableThreads;
    }

    void setPollAvailable( std::shared_ptr< ReceiverThreadState > ts, bool val ){
        if( (ts->pollAvailable) && !val )
            pollableThreads--;
        else if( !(ts->pollAvailable) && val )
            pollableThreads++;     

        ts->pollAvailable = val;
    }

public:
    const static size_t DEFAULT_BUFFSIZE = 256;
    const static size_t DEFAULT_VERBOSITY = 2;

    /*! Constructor. 
     *  - Creates initial buffer.
     */  
    MessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
                    std::initializer_list<MessType>&& initialMess = {},
                    int _verbosity = DEFAULT_VERBOSITY )
    : ringBuffer( initialMess ), writePos( initialMess.size() ), 
      itemCount( initialMess.size() ), verb( _verbosity ),
      receiverThreads( 
        []( const std::shared_ptr<ReceiverThreadState>& item1,
            const std::shared_ptr<ReceiverThreadState>& item2 ) -> bool
        { return (*item1) < (*item2); } 
      )
    {
        if( ringBuffer.size() < buffSize ){
            ringBuffer.reserve( buffSize );
            for( size_t i = 0; i < (buffSize - ringBuffer.size()); i++ ){
                ringBuffer.push_back( MessType() );
            }
        }
    }

    MessageService( const MessageService& ) = delete;
    MessageService& operator=( const MessageService& ) = delete;

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

            vlog( 2, verb, "[dispatchMsg]: (%d), writePos: %d, readPos: %d, itemCount: %d\n",
                  ringBuffer[ writePos - 1 ], writePos, readPos, itemCount );
        }
        // Wake up waiting threads to procceed with message consuming.
        cond.notify_all(); 
    }

    bool pollMessage( MessType& mess ){
        if( dispatcherClosed && !itemCount ){
            vlog( 1, verb, " [pollMsg]: Dispatcher is Closed! No more messages to expect!\n" );
            return false;
        }

        {
            std::unique_lock< std::mutex > lock( mut );
            auto&& threadID = std::this_thread::get_id();
            auto&& tIter = findReceiverThread( threadID );

            if( tIter == receiverThreads.end() ){
                vlog(0,verb,"[pollMsg]: Thread %s hasn't subscribed!\n",toString(threadID));
                return false;
            }

            std::shared_ptr< ReceiverThreadState > threadState = *tIter;

            // Check if poll is available for this thread (hasn't already polled current msg.)
            if( itemCount && threadState->pollAvailable ) 
            {
                setPollAvailable( threadState, false );

                if( readPos >= ringBuffer.size() )
                    readPos = 0;

                mess = ringBuffer[ readPos ]; 

                vlog(2, verb," [pollMsg] (%d): Thread %s RECEIVED this the first time. \n" \
                             "  writePos: %d, readPos: %d, itemCount: %d\n", 
                    (int)ringBuffer[readPos], toString(threadID).c_str(), 
                    writePos, readPos, itemCount ); 

                // Check if all receivers have polled the current message, and if so,
                // reset the poll counters, and make threads poll again.
                checkResetPolls();
                 
                return true;
            }
            
            // If thread has already received the message (or no available), 
            // wait until new message appears - new poll becomes available.
            while( !(threadState->pollAvailable) && threadState->isSubscribed && 
                   !(dispatcherClosed && !itemCount) )
            {
                cond.wait( lock );
            }
        }

        // Get the "next" message. ("Next" is now the current).
        return pollMessage( mess );
    } 

    /*! Critical sections occur in this function.
     *  We use Condition Variable to wait until all callers have received
     *  the current message on a pointer.
     */ 
    /*bool pollMessage_DOOT( MessType& mess ){
        if( dispatcherClosed && !itemCount ){
            vlog( 1, verb, " [pollMsg]: Dispatcher is Closed! No more messages to expect!\n" );
            return false;
        }

        // Lock scope.
        {
            std::unique_lock< std::mutex > lock( mut );
            auto threadID = std::this_thread::get_id();

            // Several possible situations can occur at this point:
            // 1) No items in the buffer.
            // 2) Are items, thread hasn't yet consumed the current item.
            // 3) Are items, this thread has already consumed the curr. item, and 
            //    there are still threads which haven't consumed it.
            
            // Wait while next item is not yet available, but items are still expected.
            while( !nextItemAvailable && !(dispatcherClosed && !itemCount) ){
                cond.wait( lock );                
            }

            // The first case here. Wait until there are items in a buff0r.

            // Second case here. Items present. Check if thread hasn't yet consumed the
            // current message. If this thread hasn't yet received the current message
            // (it's ID is still in "threads which haven't received current message"), 
            // get the message, and return.
            if( threadsNotReceived.find( threadID ) != threadsNotReceived.end() && itemCount ) {
                threadsNotReceived.erase( threadID );

                // Get the current message.
                if( readPos >= ringBuffer.size() )
                    readPos = 0;

                mess = ringBuffer[ readPos ]; 

                vlog(2, verb," [pollMsg] (%d): Thread %s RECEIVED this the first time. \n" \
                             "  writePos: %d, readPos: %d, itemCount: %d\n", 
                    (int)ringBuffer[readPos], toString(threadID).c_str(), 
                    writePos, readPos, itemCount ); 

                // Check if it's the last thread which received current message.
                // If so, reset lists, and notify the waiting threads to procceed 
                // with the next message.
                if( threadsNotReceived.empty() ){
                    threadsNotReceived = receiverThreadIDs;        
                    readPos++;
                    if( itemCount > 0 )
                        itemCount--;  

                    vlog(2, verb, "\nThis was the last thread which received the message! "\
                         "Resetting counters, procceeding with the next message.\n");

                    // Unlock the mutecks, and safely notify other threads to continue,
                    // just before returning.
                    resetNeeded = true;

                    lock.unlock();
                    cond.notify_all();
                } 
                return true;
            }

            vlog(2, verb," [pollMsg]: %d Threads still haven't read current msg. " \
                 "Curr. thread (%s) waiting.\n  writePos: %d, readPos: %d, itemCount: %d\n", 
                 threadsNotReceived.size(), toString(threadID).c_str(),
                 writePos, readPos, itemCount );

            // Third case. There are items, but this thread has already consumed 
            // the current message, and there are still other threads which haven't 
            // consumed the curr. message.
            // So, wait until there are no more threads that haven't consumed it.
            //   
            // - Condition variable unlocks the mutex, allowing other threads to move 
            //   freely, and waits until notification by calling notify() method.
            // - After waiting, the mutex lock is automatically Re-Acquired.
            //
            while( !resetNeeded && !threadsNotReceived.empty() && 
                   !(dispatcherClosed && !itemCount) )
            {
                cond.wait( lock );

                vlog( 4, verb, " [WAIT PollMsg]: Notified! Checking vals: " \
                      "tnr.empty(): %d, dispatcherClosed: %d\n", threadsNotReceived.empty(),
                      dispatcherClosed );
            }

            if( resetNeeded )
                resetNeeded = false;

            // At this point, all threads have received the current message.
            // We assume many receivers were waiting, so them all now are gonna procceed to the
            // next message.
            
            vlog( 3, verb," [pollMsg]: Thread %s is OUT OF WAIT! Proceeding to next msg.\n",
                  toString(threadID).c_str() );
        }

        // Get the "next" message. ("Next" is now the current).
        return pollMessage( mess );
    }*/

    /*! "Subscribe" to message service. 
     *  - Adds the calling thread's ID to the Receiver Threads lists.
     */ 
    bool subscribe(){
        std::lock_guard< std::mutex > lock( mut );
        auto threadID = std::this_thread::get_id();
        receiverThreads.insert( std::make_shared< ReceiverThreadState >( threadID ) );
    }

    /*! "UnSubscribe" from the message service. 
     *  - Removes the calling thread's ID from the Receiver Threads lists.
     */ 
    void unSubscribe(){
        std::lock_guard< std::mutex > lock( mut );

        auto&& iter = findReceiverThread( std::this_thread::get_id() );
        if( iter != receiverThreads.end() ){
            // Mark thread as no longer subscribed, and erase from the list.
            (*iter)->isSubscribed = false;
            receiverThreads.erase( iter );
        }
    }

    /*! Marks dispatcher as "closed", therefore no more message polls will be accepted.
     */ 
    void closeDispatcher(){
        dispatcherClosed = true;

        vlog( 2, verb, "\nDispatcher is closing!\n" );

        // To prevent DeadLocks, notify the waiters to check the condition.
        cond.notify_all();
    }
     
};


/*! Wrapper around the service, providing only dispatching capabilities.
 */ 
template<typename MessType>
class MessageDispatcher{
private:
    MessageService<MessType>& serv;

public:
    MessageDispatcher( MessageService<MessType>& service ) : serv( service ) 
    {}
    MessageDispatcher( const MessageDispatcher& ) = delete;

    void dispatchMessage( MessType&& mess ){
        serv.dispatchMessage( std::move(mess) );
    }

    void closeDispatcher(){
        serv.closeDispatcher();
    }
};

/*! Wrapper around the service, providing only receiving capabilities.
 */ 
template<typename MessType>
class MessageReceiver{
private:
    MessageService<MessType>& serv;

public:
    MessageReceiver( MessageService<MessType>& service ) : serv( service ) 
    {}
    MessageReceiver( const MessageReceiver& ) = delete;

    bool pollMessage( MessType& mess ){
        return serv.pollMessage( mess );
    }

    bool subscribe(){
        return serv.subscribe();
    }

    void unSubscribe(){
        serv.unSubscribe();
    }
};


/*! Demonstration function receiving messages.
 *  - Prints stuff to console according to messages.
 *  - NOTE: We use Pass By Value, because we construct the "MessageReceiver"
 *          on call, by passing it's construction parameter, MessageService.
 *    Using actual values constructed on this function has benefits of preserving the
 *    object for as long as function executes, as we execute it on a thread.
 */ 
void receiverRunner( std::shared_ptr< MessageReceiver<int> > rec, size_t waitTime = 0 ){
    int msg;
    rec->subscribe();
    while( rec->pollMessage( msg ) ){
        if( msg == 0 )
            std::cout << "[Thread "<< std::this_thread::get_id() <<"]: Hello!\n";
        else if( msg == 1 )
            std::cout << "[Thread "<< std::this_thread::get_id() <<"]: World!\n"; 
        else
            std::cout << "[Thread "<< std::this_thread::get_id() <<"]: Unknown message.\n"; 

        if( waitTime )
            std::this_thread::sleep_for( std::chrono::milliseconds( waitTime ) );
    }
    rec->unSubscribe();
}

/*! Demonstration function dispatching messages.
 */ 
void dispatcherRunner( std::shared_ptr< MessageDispatcher<int> > dis, size_t waitTime = 0 ){
    const size_t iters = 2;
    for( size_t i = 0; i < iters; i++ ){
        dis->dispatchMessage( i % 2 );
        if( waitTime )
            std::this_thread::sleep_for( std::chrono::milliseconds( waitTime ) );
    }
    dis->closeDispatcher();
}

/*! Launches the message dispath thread
 *  and all message receiver threads.
 */
int main(){
    const size_t receiverCount = 10;

    // Create a message communication service.
    MessageService<int> service;

    // Spawn receivers.
    std::vector< std::thread > receiverPool;
    receiverPool.reserve( receiverCount );
    for( size_t i = 0; i < receiverCount; i++ ){
        receiverPool.push_back( std::thread( receiverRunner, 
            std::make_shared< MessageReceiver<int> >( service ), 0 ) );
    }

    // Dispatcher - this thread.
    dispatcherRunner( std::make_shared< MessageDispatcher<int> >( service ), 0 );

    // Join all threads.
    for( auto&& th : receiverPool ){
        th.join();
    }

    return 0;
}

