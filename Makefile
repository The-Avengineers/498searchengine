AVENGINE_SRC := avengine
BUILD_DIR := build

.PHONY: test configure build clean

test: configure build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --no-tests=error

configure:
	cmake -S $(AVENGINE_SRC) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build:
	cmake --build $(BUILD_DIR) --config Release -j

clean:
	rm -rf $(BUILD_DIR)
