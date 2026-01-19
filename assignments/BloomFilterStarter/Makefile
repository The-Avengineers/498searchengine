CXXFLAGS = -std=c++17 -Wall -Werror -pedantic -O3
LIBS = -ldl -lssl -lcrypto

bf: main.cpp
	g++ $(CXXFLAGS) $^ $(LIBS) -o $@ 

clean:
	rm -rf bf