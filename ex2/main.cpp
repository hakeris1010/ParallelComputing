#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <vector>

template<class MessType>
class MessageService{
private:
    std::vector< MessType > ringBuffer;
    size_t readPos = 0;
    size_t writePos = 0;

protected:
    void dispatchMessage( const MessType& mess );
    void dispatchMessage( MessType&& mess );

public:
    MessageService( size_t buffSize, std::initializer_list<MessType>&& initialMess = {} )
    : ringBuffer( initialMess ), writePos( initialMess.size() ) {
        if( ringBuffer.size() < buffSize ){
            ringBuffer.reserve( buffSize );
            for( size_t i = 0; i < (buffSize - ringBuffer.size()); i++ ){
                ringBuffer.push_back( MessType() );
            }
        }
        if( writePos >= ringBuffer.size() )
            writePos = 0;
    }

    void pollMessage( MessType& mess );
};

template<typename MessType>
class MessageDispatcher : public MessageService{

}

/*! Launches the message dispath thread
 *  and all message receiver threads.
 */
int main(){
    size_t receiverCount = 10;

    return 0;
}

