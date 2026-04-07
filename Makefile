
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


ifeq (windows,$(findstring windows,$(PRESET)))

EXEFILE=$(BUILD_PATH)/$(BUILD_TYPE)/jsengine.exe

$(EXEFILE): build

run: $(EXEFILE)
	$(EXEFILE)

endif
