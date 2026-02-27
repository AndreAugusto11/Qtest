
clean:
	-rm -rf build


build: clean
	-mkdir -p ./build
	@set -e; \
	for src in $$(find ./src -name "*.cpp" ! -name "bridge.cpp"); do \
		rel="$${src#./src/}"; \
		mkdir -p "./build/$$(dirname "$${rel}")"; \
		output=$$(cdt-cpp "$$src" -o "./build/$$(basename "$$src" .cpp).wasm" -I ./include/ -I ./external/ -DCHAIN_ID=41 -abigen_output "./build/$$(basename "$$src" .cpp).abi" 2>&1); \
		echo "$$output"; \
		echo "$$output" | grep -Eq "(^|[[:space:]])error:" && exit 1 || true; \
	done
