AVENGINE_SRC := .
BUILD_DIR := build

.PHONY: test test-verbose configure build clean

test: configure build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --no-tests=error

test-verbose: configure build
	ctest --test-dir $(BUILD_DIR) --output-on-failure --no-tests=error -VV

configure:
	@if [ -f $(BUILD_DIR)/CMakeCache.txt ] && ! grep -Fxq "CMAKE_HOME_DIRECTORY:INTERNAL=$(CURDIR)" $(BUILD_DIR)/CMakeCache.txt; then \
		echo "Removing stale $(BUILD_DIR)/ configured for a different source root"; \
		rm -rf $(BUILD_DIR); \
	fi
	cmake -S $(AVENGINE_SRC) -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR) --config Release -j

clean:
	rm -rf $(BUILD_DIR)
