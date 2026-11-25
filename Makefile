# SAFE-NODE convenience targets. See README.md for the full build guide.
PY ?= python3

.PHONY: help test test-fw test-host vectors budget demo node gateway clean

help:
	@echo "make test       firmware unit tests (gcc) + host tests (pytest)"
	@echo "make demo       run the host with the built-in gateway emulator -> http://127.0.0.1:5000"
	@echo "make budget     reproduce DPR Table X analytical figures"
	@echo "make vectors    regenerate test/vectors/frames.txt"
	@echo "make node       idf.py build for firmware/node/v1.0   (needs ESP-IDF v5 exported)"
	@echo "make gateway    idf.py build for firmware/gateway     (needs ESP-IDF v5 exported)"

test: test-fw test-host

test-fw:
	$(MAKE) -C firmware/test

test-host:
	cd host && $(PY) -m pytest -q

vectors:
	$(PY) tools/gen_vectors.py

budget:
	$(PY) tools/lora_budget.py --check

demo:
	cd host && $(PY) -m safenode_host --demo --no-mqtt

node:
	cd firmware/node/v1.0 && idf.py set-target esp32 build

gateway:
	cd firmware/gateway && idf.py set-target esp32 build

clean:
	$(MAKE) -C firmware/test clean
	rm -rf firmware/node/v1.0/build firmware/gateway/build
