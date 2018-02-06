/*! Program demonstrates the problem of Critical Section.
 *  - To demostrate it, we print data to a string: 
 *    print a line "Hello World !" in 4 stages.
 *  - Synchronized and Non-Synchronized execution modes are available.
 */

#include <iostream>
#include <thread>
#include <mutex>
#include <cstring>
#include <string>
#include <vector>

bool verbose = false;
bool synced = false;
size_t threadCount = 20;

struct MutexedString{
    std::mutex mut;
    std::string str;
};

/*! Critical Section occurs in this function, which prints
 *  "Hello World!" in 4 stages.  
 */
void printHelloWorld( std::shared_ptr<MutexedString> mts ){
    mts->str.append("Hello ");
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    mts->str.append("World ");
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    mts->str.append("! ");
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );

    mts->str.append("\n");
    std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
}

void printHelloWorldMutexed( std::shared_ptr<MutexedString> mts ){
    // Synchronize the output by locking a mutex, forcing other threads to wait.
    std::lock_guard<std::mutex> lock( mts->mut );
    printHelloWorld( mts );
}

int main(int argc, char** argv){
    if(argc > 1){
        for(int i = 0; i < argc; i++){
            if( strcmp( argv[i], "--synced" ) == 0 )
                synced = true;
            if( strcmp( argv[i], "--verbose" ) == 0 || strcmp( argv[i], "-v" ) == 0)
                verbose = true;
        }
    }

    std::shared_ptr<MutexedString> mts = std::make_shared<MutexedString>();

    std::vector< std::thread > pool;
    for( size_t i = 0; i < threadCount; i++ ){
        pool.push_back( std::thread( (synced ? printHelloWorldMutexed : printHelloWorld), mts ) );
    }

    for( auto&& th : pool )
        th.join();

    std::cout << mts->str << "\n";

    return 0;
}

