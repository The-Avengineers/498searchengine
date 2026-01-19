#include "Utf8.h"
#include <iostream>
#include <fstream>
#include <sstream>
 
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: flip <filename>\nRecognize the input file as either" <<
        " Utf8 or Utf16, convert it to the opposite encoding, and write the result to stdout\n";
        return 1;
    }
    std::ifstream inputFile(argv[1]);
    if (!inputFile.is_open()) {
        std::cerr << argv[1] << " not found.\n" << std::endl;
        return 1;
    }   
    std::string content;
    std::ostringstream oss; 
    oss << inputFile.rdbuf();
    content = oss.str();
    inputFile.close();

    std::cout << "File Contents:\n" << content << std::endl;
}