#include "Utf8.h"
#include <iostream>
#include <fstream>
#include <sstream>

void convertUtf8toUtf16(const std::string& content) {
    // When writing Utf16, use little-endian format. 
    const Utf8* p = reinterpret_cast<const Utf8*>(content.data());
    const Utf8* bound = p + content.size();
    while (p < bound) {
        Unicode c = ReadUtf8(&p, bound);
        Utf16 buf[2];
        Utf16* end = WriteUtf16(buf, c, buf + 2);
        for (Utf16* q = buf; q < end; ++q) {
            // little-endian
            char bytes[2];
            bytes[0] = static_cast<char>(*q & 0xff);
            bytes[1] = static_cast<char>((*q >> 8) & 0xff);
            std::cout.write(bytes, 2);
        }
    }
}

void convertUtf16toUtf8(const std::string& content, bool is_big_endian) {
    // If the BOM indicates you’re reading big-endian
    // Utf16, your flip utility should flip the byte sex of the data before decoding.
    const size_t byte_len = content.size();
    size_t char_loc = 0;
    while (char_loc + 1 < byte_len) {
        const unsigned char b0 = static_cast<unsigned char>(content[char_loc]);
        const unsigned char b1 = static_cast<unsigned char>(content[char_loc + 1]);
        Utf16 units[2];
        if (is_big_endian) {
            units[0] = static_cast<Utf16>((b0 << 8) | b1);
        } else {
            units[0] = static_cast<Utf16>((b1 << 8) | b0);
        }
        size_t unit_count = 1;
        if (char_loc + 3 < byte_len) {
            const unsigned char b2 = static_cast<unsigned char>(content[char_loc + 2]);
            const unsigned char b3 = static_cast<unsigned char>(content[char_loc + 3]);
            if (is_big_endian) {
                units[1] = static_cast<Utf16>((b2 << 8) | b3);
            } else {
                units[1] = static_cast<Utf16>((b3 << 8) | b2);
            }
            unit_count = 2;
        }
        const Utf16* p = units;
        const Utf16* bound = units + unit_count;
        Unicode c = ReadUtf16(&p, bound);
        Utf8 buf[6];
        Utf8* end = WriteUtf8(buf, c, buf + 6);
        std::cout.write(reinterpret_cast<const char*>(buf), end - buf);
        char_loc += static_cast<size_t>(p - units) * 2;
    }
}

 
int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: flip <filename>\nRecognize the input file as either" <<
        " Utf8 or Utf16, convert it to the opposite encoding, and write the result to stdout\n";
        return 1;
    }
    std::ifstream inputFile(argv[1], std::ios::binary);
    if (!inputFile.is_open()) {
        std::cerr << argv[1] << " not found.\n" << std::endl;
        return 1;
    }   
    std::string content;
    std::ostringstream oss;
    oss << inputFile.rdbuf();
    content = oss.str();
    inputFile.close();
    bool is_utf16 = false;
    bool is_big_endian = false;

    if (content.size() <= 2) {
        return 0; // file is empty TODO: check how to handle this scenario
    }

    const unsigned char b0 = static_cast<unsigned char>(content[0]);
    const unsigned char b1 = static_cast<unsigned char>(content[1]);
    if (b0 == 0xff && b1 == 0xfe) {
        is_utf16 = true;
        is_big_endian = false;
    } else if (b0 == 0xfe && b1 == 0xff) {
        is_utf16 = true;
        is_big_endian = true;
    } else if (content.size() >= 3 &&
                b0 == Utf8BOMString[0] &&
                b1 == Utf8BOMString[1] &&
                static_cast<unsigned char>(content[2]) == Utf8BOMString[2]) {
        is_utf16 = false;
    }

    // std::cout << "File Contents:\n" << content << std::endl;
    // std::cout << "is_utf16:\n" << is_utf16 << std::endl;
    // std::cout << "is_big_endian:\n" << is_big_endian << std::endl;
    if (!is_utf16) {
        convertUtf8toUtf16(content); // When writing Utf16, use little-endian format. 
    } else {
        convertUtf16toUtf8(content, is_big_endian);
    }
}
