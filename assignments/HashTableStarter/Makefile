CXX - g++ --std=c++17

all:  Top10 HashTable HashBlob HashFile

Top10: Top10.cpp Common.h Common.cpp TopN.h TopN.cpp HashTable.h 
	$(CXX) Top10.cpp Common.cpp TopN.cpp -o Top10

HashTable: HashTable.cpp Common.h Common.cpp HashTable.h Timer.h
	$(CXX) HashTable.cpp Common.cpp -o HashTable

HashBlob:  HashBlob.cpp HashTable.h HashBlob.h Common.h Common.cpp Timer.h
	$(CXX) HashBlob.cpp Common.cpp -o HashBlob

HashFile:  HashFile.cpp HashTable.h HashBlob.h Common.h Timer.h
	$(CXX) HashFile.cpp Common.cpp -o HashFile

clean:
	rm -f Top10 HashTable HashBlob HashFile