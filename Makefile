.PHONY: build-cpp build-go clean

BUILD_DIR=cpp/build
LIB_NAME=libpedalboard_static.a

build-cpp:
	mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. && make

build-go: build-cpp
	go build -v ./...

test: build-cpp
	go test -v ./...

clean:
	rm -rf $(BUILD_DIR)
	rm -f pkg/pedalboard/pedalboard.a
