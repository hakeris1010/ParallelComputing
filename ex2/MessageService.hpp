#ifndef MESSAGE_SERVICE_INCLUDED
#define MESSAGE_SERVICE_INCLUDED

/*! Various utilities needed for project:
 *  - Formatted conditional Logging
 *  - Generic toString().
 */ 
namespace Util{
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
}

/*! Basic ThreadState
 */ 
struct ThreadState{
    std::thread::id id;
    volatile bool isSubscribed = true;

    bool operator< (const ThreadState& other) const {
        return id < other.id;
    }
    bool operator== (const ThreadState& other) const {
        return id == other.id;
    } 

    ThreadState( std::thread::id _id ) : id( _id ) {}
};

/*! Thread State comparation lambdas.
 */ 
constexpr auto compareLambda = 
    []( const std::shared_ptr<ThreadState>& item1, 
        const std::shared_ptr<ThreadState>& item2 ) -> bool
    { return (*item1) < (*item2); }; 


/*! Message dispatching/receiving service, created for multithreaded applications.
 *  - Some implementations Uses Thread IDs heavily.
 *  - Enables communication between threads and optimized messaging.
 */ 
template<class MessType>
class MessageService{
public:
    virtual void dispatchMessage( MessType&& mess ) = 0;
    virtual bool pollMessage( MessType& mess ) = 0;
    virtual bool subscribe() = 0;
    virtual void unSubscribe() = 0;
    virtual void closeDispatcher() = 0;
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

#endif // MESSAGE_SERVICE_INCLUDED

