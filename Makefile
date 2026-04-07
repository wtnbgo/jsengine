
ifeq ($(shell type cygpath > /dev/null && echo true),true)
FIXPATH = cygpath -ma
else
FIXPATH = realpath
endif

# Detect OS and set default PRESET accordingly
ifeq ($(OS),Windows_NT)
	PRESET?=x64-windows
else
	UNAME_S := $(shell uname -s)
	ifeq ($(UNAME_S),Linux)
		PRESET?=x64-linux
	else ifeq ($(UNAME_S),Darwin)
		PRESET?=x64-macos
	else
		PRESET?=x64-windows
	endif
endif

BUILD_TYPE?=Release
CMAKEOPT?=
INSTALL_PREFIX?=install

export BUILD_TYPE

BUILD_PATH=$(shell cmake --preset $(PRESET) -N | grep BUILD_DIR | sed 's/.*BUILD_DIR="\(.*\)"/\1/')

.PHONY: build  prebuild

all: build

# cmake 処理実行
# CMAKEOPT で引数定義追加
prebuild:
	cmake --preset $(PRESET) $(CMAKEOPT)

# ビルド実行
build:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE)

clean:
	cmake --build $(BUILD_PATH) --config $(BUILD_TYPE) --target clean

install:
	cmake --install $(BUILD_PATH) --config $(BUILD_TYPE) --prefix $(INSTALL_PREFIX)

# three.js ES5 トランスパイル
# 前提: npm install --save-dev @babel/core @babel/cli @babel/preset-env
THREEJS_SRC = data/lib/three.min.js
THREEJS_ES5 = data/lib/three.es5.js

$(THREEJS_ES5): $(THREEJS_SRC)
	npx babel $(THREEJS_SRC) --presets=@babel/preset-env -o $(THREEJS_ES5)

transpile: $(THREEJS_ES5)

# three.min.js のダウンロード（未取得時）
$(THREEJS_SRC):
	curl -sL "https://cdn.jsdelivr.net/npm/three@0.128.0/build/three.min.js" -o $(THREEJS_SRC)

# npm 依存のセットアップ
setup-npm:
	npm install --save-dev @babel/core @babel/cli @babel/preset-env

ifeq (windows,$(findstring windows,$(PRESET)))

EXEFILE=$(BUILD_PATH)/$(BUILD_TYPE)/jsengine.exe

$(EXEFILE): build

run: $(EXEFILE)
	$(EXEFILE)

endif
