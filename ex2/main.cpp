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
    volatile bool pollAvailable = true;
    volatile bool isSubscribed = true;

    bool operator< (const ReceiverThreadState& other) const {
        return id < other.id;
    }
    bool operator== (const ReceiverThreadState& other) const {
        return id == other.id;
    } 

    ReceiverThreadState( std::thread::id _id ) : id( _id ) {}
};

/*! TODO:
 * Pollability Control & Configuration classes.
 * - Checks and controls message receiver's ability to poll new messages.
 * - pollAvailable( id ) indicate whether this ID is able to poll a new message,
 *   must wait, or is no longer allowed to poll at all.  
 */ 

/*! Thread State comparation lambda.
 */ 
constexpr auto compareLambda = 
    []( const std::shared_ptr<ReceiverThreadState>& item1, 
        const std::shared_ptr<ReceiverThreadState>& item2 ) -> bool
    { return (*item1) < (*item2); };

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

    std::set< std::shared_ptr<ReceiverThreadState>, decltype(compareLambda) > receiverThreads;

    size_t pollableThreads = 0;

    /*! These private functions assume lock is already acquired.
     * - Returns an iterator to std::shared_ptr to threadState object with id = id.
     *  @param id - Thread's ID.
     */ 
    auto findReceiverThread( std::thread::id id ){
        return receiverThreads.find( std::make_shared< ReceiverThreadState >( id ) );
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
    const static size_t DEFAULT_VERBOSITY = 0;

    /*! Constructor. 
     *  - Creates initial buffer.
     */  
    MessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
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

            vlog( 2, verb, "[dispatchMsg]: (%p), writePos: %d, readPos: %d, itemCount: %d\n",
                  &ringBuffer[ writePos - 1 ], writePos, readPos, itemCount );
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
                vlog( 0, verb, "[pollMsg]: Thread %s hasn't subscribed!\n",
                      toString(threadID).c_str() );
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

                vlog(2, verb," [pollMsg] (%p): Thread %s RECEIVED this the first time. \n" \
                             "  writePos: %d, readPos: %d, itemCount: %d\n", 
                    &ringBuffer[readPos], toString(threadID).c_str(), 
                    writePos, readPos, itemCount ); 

                // Check if all receivers have polled the current message, and if so,
                // reset the poll counters, and make threads poll again.
                checkResetPolls();
                 
                return true;
            }
            
            // If thread has already received the message (or no available), 
            // wait until new message appears - new poll becomes available.
            while( ( !(threadState->pollAvailable) || !itemCount ) && 
                   threadState->isSubscribed && !(dispatcherClosed && !itemCount) )
            {
                cond.wait( lock );
            }
        }

        // Get the "next" message. ("Next" is now the current).
        return pollMessage( mess );
    } 

    /*! "Subscribe" to message service. 
     *  - Adds the calling thread's ID to the Receiver Threads lists.
     */ 
    bool subscribe(){
        std::lock_guard< std::mutex > lock( mut );

        // Create new Thread State, and increment pollable thread counter.
        auto thState = std::make_shared<ReceiverThreadState>( std::this_thread::get_id() );
        auto res = receiverThreads.insert( thState );

        // insert() returns std::pair, with second element indicating if new 
        // element was inserted to a set.
        if( res.second ){
            thState->pollAvailable = true;
            pollableThreads++;
        }
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
template< class MSG >
void receiverRunner( std::shared_ptr< MessageReceiver<MSG> > rec, size_t waitTime = 0 ){
    static std::mutex outmut;
    static volatile size_t maxcount = 0;

    thread_local size_t count = 0;
    MSG msg;
    
    rec->subscribe();
    while( rec->pollMessage( msg ) ){
        {
            // Lock the critical section - printing data and reading/modifying maxcount.
            std::lock_guard< std::mutex > lock( outmut );

            // If current thread's counter is higher than the thread's with highest 
            // read message counter, it means our thread is first to poll a new message.
            count++;
            if( count > maxcount ){
                maxcount = count;
                std::cout << "\n";
            } 

            // And print the message, flushing cout at the end.
            std::cout << "[Thread "<< std::this_thread::get_id() <<"]: "<< msg <<"\n";
            std::cout << std::flush; 
        }

        if( waitTime )
            std::this_thread::sleep_for( std::chrono::milliseconds( waitTime ) );
    }
    rec->unSubscribe();
}

/*! Demonstration function dispatching messages.
 */ 
void dispatcherRunner( std::shared_ptr< MessageDispatcher< std::string > > dis, 
                       size_t waitTime = 0 )
{
    const size_t iters = 3;
    for( size_t i = 0; i < iters; i++ ){
        if( waitTime )
            std::this_thread::sleep_for( std::chrono::milliseconds( waitTime ) ); 
         
        std::string msg = ( i%2 ? "Hello" : "World" );

        dis->dispatchMessage( std::move( msg ) );
    }
    dis->closeDispatcher();
}

/*! Launches the message dispath thread
 *  and all message receiver threads.
 */
int main(){
    const size_t receiverCount = 10;

    // Create a message communication service.
    MessageService< std::string > service;

    // Spawn receivers.
    std::vector< std::thread > receiverPool;
    receiverPool.reserve( receiverCount );
    for( size_t i = 0; i < receiverCount; i++ ){
        receiverPool.push_back( std::thread( receiverRunner<std::string>, 
            std::make_shared< MessageReceiver< std::string > >( service ), i*50 ) );
    }

    // Dispatcher - this thread.
    dispatcherRunner( std::make_shared< MessageDispatcher<std::string> >( service ), 30 );

    // Join all threads.
    for( auto&& th : receiverPool ){
        th.join();
    }

    return 0;
}

