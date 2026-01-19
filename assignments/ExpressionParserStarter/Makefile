CXX = g++
CXXFLAGS = -std=c++17 -g

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $*.cpp -o $*.o

driver: parser.o tokenstream.o expression.o driver.cpp
	$(CXX) $(CXXFLAGS) $^ -o driver

.PHONY: clean

clean:
	rm -f parser.o tokenstream.o expression.o driver
