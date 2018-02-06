#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <set>

template<class MessType>
class MessageService{
private:
    std::vector< MessType > ringBuffer;
    size_t readPos = 0;
    size_t writePos = 0;
    size_t itemCount = 0;

    volatile bool dispatcherClosed = false;

    std::mutex mut;
    std::condition_variable cond;

    std::set< std::thread::id > receiverThreadIDs;
    std::set< std::thread::id > threadsNotReceived;

protected:
    const static size_t DEFAULT_BUFFSIZE = 256;

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
        }
        // Wake up waiting threads to procceed with message consuming.
        cond.notify_all(); 
    }

    /*! Critical sections occur in this function.
     *  We use Condition Variable to wait until all callers have received
     *  the current message on a pointer.
     */ 
    bool pollMessage( MessType& mess ){
        if( dispatcherClosed )
            return false;

        // Lock scope.
        {
            std::unique_lock< std::mutex > lock( mut );
            auto threadID = std::this_thread::get_id();

            // If this thread hasn't yet received the current message
            // (it's ID is still in "threads which haven't received current message"), 
            // get the message, and return.
            if( threadsNotReceived.find( threadID ) != threadsNotReceived.end() && itemCount ) {
                threadsNotReceived.erase( threadID );

                if( readPos >= ringBuffer.size() )
                    readPos = 0;

                // Get message, and return it.
                mess = ringBuffer[ readPos ]; 
                return true;
            }

            // If this thread has already consumed the current message, wait until 
            // there are no more threads that haven't consumed the message yet.
            //
            // - The lambda passed indicates the condition of wait end:
            //   while (!pred()) wait(lck);
            //   
            // - Condition variable unlocks the mutex, allowing other threads to move 
            //   freely, and waits until notification by calling notify() method.
            //
            cond.wait( lock, [&](){ 
                // Wait Stop Condition.
                return threadsNotReceived.empty() && itemCount > 0;
            } );
            // At this point, all threads have received the current message.
            // After waiting, the mutex lock is automatically Re-Acquired.
            // We assume many receivers were waiting, so them all now are gonna procceed to the
            // next message.

            // ReSet the list of threads which haven't still received the message.
            threadsNotReceived = receiverThreadIDs;        
             
            readPos++;
            itemCount--; 
        }

        // Get the next message.
        return pollMessage( mess );
    }

    /*! "Subscribe" to message service. 
     *  - Adds the calling thread's ID to the Receiver Threads lists.
     */ 
    bool subscribe(){
        std::unique_lock< std::mutex > lock( mut );
        auto threadID = std::this_thread::get_id();
        
        receiverThreadIDs.insert( threadID );
        threadsNotReceived.insert( threadID );
    }

    /*! "UnSubscribe" from the message service. 
     *  - Removes the calling thread's ID from the Receiver Threads lists.
     */ 
    bool unSubscribe(){
        std::unique_lock< std::mutex > lock( mut );
        auto threadID = std::this_thread::get_id();

        receiverThreadIDs.erase( threadID );
        threadsNotReceived.erase( threadID );
    }

    void markDispatcherAsClosed(){
        dispatcherClosed = true;
    }

public:
    MessageService( size_t buffSize = DEFAULT_BUFFSIZE, 
                    std::initializer_list<MessType>&& initialMess = {} )
    : ringBuffer( initialMess ), writePos( initialMess.size() ), 
      itemCount( initialMess.size() ) 
    {
        if( ringBuffer.size() < buffSize ){
            ringBuffer.reserve( buffSize );
            for( size_t i = 0; i < (buffSize - ringBuffer.size()); i++ ){
                ringBuffer.push_back( MessType() );
            }
        }
    }
     
};


/*template<typename MessType>
class MessageDispatcher : public MessageService{

};*/

/*! Launches the message dispath thread
 *  and all message receiver threads.
 */
int main(){
    size_t receiverCount = 10;

    return 0;
}

