#include <iostream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>
#include <algorithm>

template<class MessType>
class MessageService{
private:
    std::vector< MessType > ringBuffer;
    size_t readPos = 0;
    size_t writePos = 0;
    size_t itemCount = 0;

    std::mutex mut;
    std::condition_variable cond;

    std::vector< std::thread::id > receiverThreadIDs;
    std::vector< std::thread::id > threadsNotReceived;

protected:
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

public:
    MessageService( size_t buffSize, std::initializer_list<MessType>&& initialMess = {} )
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

    /*! Critical sections occur in this function.
     *  We use Condition Variable to wait until all callers have received
     *  the current message on a pointer.
     */ 
    bool pollMessage( MessType& mess ){
        std::unique_lock< std::mutex > lock( mut );
        auto threadID = std::this_thread::get_id();

        // If this thread hasn't yet received the current message, get it, and return.
        if( std::find( threadsNotReceived.begin(), threadsNotReceived.end(), threadID )
            != threadsNotReceived.end() ){
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
            return threadsNotReceived.empty() && itemCount > 0;
        } );
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

