CXX = g++
CXXFLAGS = -std=c++11 -g

all: LinuxGetSsl LinuxGetUrl

LinuxGetSsl: LinuxGetSsl.cpp
	$(CXX) $(CXXFLAGS) $^ -lssl -lcrypto -lz -o LinuxGetSsl

LinuxGetUrl: LinuxGetUrl.cpp
	$(CXX) $(CXXFLAGS) $^ -o LinuxGetUrl

.PHONY: clean

clean:
	rm -f LinuxGetSsl LinuxGetUrl
