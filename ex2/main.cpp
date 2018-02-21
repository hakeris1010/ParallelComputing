#include <iostream>
#include <thread>
#include <mutex>
#include "BarrierMessageService.hpp"
#include "MultiReceiveMessageService.hpp"

/*! Demonstration function receiving messages.
 *  - Prints stuff to console according to messages.
 *  - NOTE: We use Pass By Value, because we construct the "MessageReceiver"
 *          on call, by passing it's construction parameter, MessageService.
 *    Using actual values constructed on this function has benefits of preserving the
 *    object for as long as function executes, as we execute it on a thread.
 */ 
template< class MSG >
void receiverRunner( std::shared_ptr< MessageReceiver<MSG> > rec, const size_t waitTime = 0 ){
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
    const size_t iters = 5;
    for( size_t i = 0; i < iters; i++ ){
        if( waitTime )
            std::this_thread::sleep_for( std::chrono::milliseconds( waitTime ) ); 
         
        //std::string msg = ( i%2 ? "Hello" : (i%3 ? "World" : "Programming") );
        std::string msg = "[ MSG #"+ std::to_string( i ) +" ]";

        dis->dispatchMessage( std::move( msg ) );
    }
    dis->closeDispatcher();
}

/*! Launches the message dispath thread
 *  and all message receiver threads.
 */
int main(){
    const size_t receiverCount = 2;

    // Create a message communication service.
    MultiReceiverMessageService< std::string > service;
    //BarrierMessageService< std::string > service;

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

