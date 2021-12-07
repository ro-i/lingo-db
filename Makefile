ROOT_DIR := $(dir $(abspath $(firstword $(MAKEFILE_LIST))))

build/llvm-build:
	mkdir -p build/llvm-build
	cmake -G Ninja llvm-project/llvm  -B build/llvm-build \
	   -DLLVM_ENABLE_PROJECTS=mlir \
	   -DLLVM_USE_PERF=ON \
	   -DLLVM_BUILD_EXAMPLES=OFF \
	   -DLLVM_TARGETS_TO_BUILD="X86" \
	   -DCMAKE_BUILD_TYPE=Release \
	   -DLLVM_ENABLE_ASSERTIONS=ON

build/arrow:
	mkdir -p build/arrow
	cmake arrow/cpp  -B build/arrow -DARROW_GANDIVA=1 -DARROW_PYTHON=ON

build-arrow: build/arrow
	cmake --build build/arrow
	cmake --install build/arrow --prefix build/arrow/install

build/pyarrow:
	cd arrow/python; python3 setup.py build_ext --inplace --extra-cmake-args="-DArrow_DIR=${ROOT_DIR}/build/arrow/install/lib/cmake/arrow -DArrowPython_DIR=${ROOT_DIR}/build/arrow/install/lib/cmake/arrow"
build-llvm: build/llvm-build
	cmake --build build/llvm-build -j4

build/llvm-build-debug:
	mkdir -p build/llvm-build-debug
	cmake -G Ninja llvm-project/llvm  -B build/llvm-build-debug \
	   -DLLVM_ENABLE_PROJECTS=mlir \
	   -DLLVM_BUILD_EXAMPLES=OFF \
	   -DLLVM_TARGETS_TO_BUILD="X86;" \
	   -DCMAKE_BUILD_TYPE=Debug \
	   -DLLVM_ENABLE_ASSERTIONS=ON

build-llvm-debug: build/llvm-build-debug
	cmake --build build/llvm-build-debug -j1

build/build-debug-llvm-release:
	cmake -G Ninja .  -B  build/build-debug-llvm-release \
		-DMLIR_DIR=${ROOT_DIR}build/llvm-build/lib/cmake/mlir \
		-DArrow_DIR=${ROOT_DIR}build/arrow/install/lib/cmake/arrow \
		-DBoost_INCLUDE_DIR=${ROOT_DIR}build/arrow/boost_ep-prefix/src/boost_ep \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build/build-llvm-release:
	cmake -G Ninja .  -B  build/build-llvm-release \
		-DMLIR_DIR=${ROOT_DIR}build/llvm-build/lib/cmake/mlir \
		-DArrow_DIR=${ROOT_DIR}build/arrow/install/lib/cmake/arrow \
		-DBoost_INCLUDE_DIR=${ROOT_DIR}build/arrow/boost_ep-prefix/src/boost_ep \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Release

build/build-debug-llvm-release-coverage:
	cmake -G Ninja .  -B  build/build-debug-llvm-release-coverage \
		-DMLIR_DIR=${ROOT_DIR}build/llvm-build/lib/cmake/mlir \
		-DArrow_DIR=${ROOT_DIR}build/arrow/install/lib/cmake/arrow \
		-DBoost_INCLUDE_DIR=${ROOT_DIR}build/arrow/boost_ep-prefix/src/boost_ep \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_FLAGS=--coverage -DCMAKE_C_FLAGS=--coverage

dependencies: build build-llvm build-arrow build/pyarrow

test-coverage:  build/build-debug-llvm-release-coverage
	cmake --build build/build-debug-llvm-release-coverage --target mlir-db-opt db-run-query db-run pymlirdbext -- -j 6
	export LD_LIBRARY_PATH=${ROOT_DIR}/build/arrow/install/lib &&./build/llvm-build/bin/llvm-lit build/build-debug-llvm-release-coverage/test
	lcov --no-external --capture --directory build/build-debug-llvm-release-coverage -b . --output-file build/build-debug-llvm-release-coverage/coverage.info
		lcov --remove build/build-debug-llvm-release-coverage/coverage.info -o build/build-debug-llvm-release-coverage/filtered-coverage.info \
			'**/build/llvm-build/*' '**/llvm-project/*' '*.inc' '**/arrow/*' '**/pybind11/*'
	genhtml  --ignore-errors source build/build-debug-llvm-release-coverage/filtered-coverage.info --legend --title "lcov-test" --output-directory=build/build-debug-llvm-release-coverage/coverage-report
run-test: build/build-debug-llvm-release
	cmake --build build/build-debug-llvm-release --target mlir-db-opt db-run-query db-run pymlirdbext -- -j 6
	export LD_LIBRARY_PATH=${ROOT_DIR}/build/arrow/install/lib && ./build/llvm-build/bin/llvm-lit -v build/build-debug-llvm-release/test
coverage-clean:
	rm -rf build/build-debug-llvm-release-coverage/coverage

build-docker:
	docker build -f "docker/Dockerfile" -t dockerize:latest "."
clean:
	rm -rf build

#perf:
#	 perf record -k 1 -F 1000  --call-graph dwarf [cmd]
#	 perf inject -j -i perf.data -o perf.data.jitted
#	perf report --no-children -i perf.data.jitted
