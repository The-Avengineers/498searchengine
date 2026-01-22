all: flip

flip: flip.cpp Utf8.cpp Utf8.h
	g++ --std=c++17 flip.cpp Utf8.cpp -g -o flip

clean:
	rm -f flip

test: flip
	g++ utf8_tests.cpp Utf8.cpp -o utf8_tests && ./utf8_tests
	bash integration-test.sh
