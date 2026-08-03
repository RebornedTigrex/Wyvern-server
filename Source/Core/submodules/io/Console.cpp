#include<submodules/io/Console.h>

void Console::postOutput(std::string_view message, ConsoleIODeclaration declaration){
    std::lock_guard<std::mutex> lock(outputMutex);
    outputQuery.push_back(declaration);
}


void Console::postInput(std::string_view message, ConsoleIODeclaration declaration){
    
}
