# Chaos Lab sidecar — builds outside messageEngine; links libmessageengine.a only.
#
# Default layout (live engine checkout): MESSAGE_ENGINE=../messageEngine
# Sibling repos: make MESSAGE_ENGINE=../../messageEngine
# Vendored fallback: ../third_party/messageengine (library + headers snapshot)
#
# If a live engine checkout is found, scripts/build-messageengine-lib.sh can refresh
# libmessageengine.a before linking. If only vendored artifacts exist, make links them
# as-is without rebuilding.

UNAME_S := $(shell uname -s)
REPO_ROOT := $(abspath $(CURDIR)/..)
BUILD_ME_LIB := $(REPO_ROOT)/scripts/build-messageengine-lib.sh
PKG_CONFIG ?= pkg-config

MESSAGE_ENGINE ?= ../messageEngine
# Absolute path for subprocesses (build-messageengine-lib.sh / resolve).
MESSAGE_ENGINE_ABS := $(if $(filter /%,$(MESSAGE_ENGINE)),$(MESSAGE_ENGINE),$(abspath $(CURDIR)/$(MESSAGE_ENGINE)))
VENDORED_ENGINE := $(REPO_ROOT)/third_party/messageengine

ifneq ($(wildcard $(MESSAGE_ENGINE_ABS)/src/core/Types.hpp),)
ME_ROOT := $(MESSAGE_ENGINE_ABS)
ME_SOURCE := live
else ifneq ($(wildcard $(VENDORED_ENGINE)/src/core/Types.hpp),)
ME_ROOT := $(VENDORED_ENGINE)
ME_SOURCE := vendored
else
$(error messageEngine not found. Provide MESSAGE_ENGINE=<path> or add vendored artifacts at third_party/messageengine)
endif

MBEDTLS_PKG_CFLAGS := $(shell $(PKG_CONFIG) --cflags mbedtls mbedx509 mbedcrypto 2>/dev/null)
MBEDTLS_PKG_LIBS   := $(shell $(PKG_CONFIG) --libs mbedtls mbedx509 mbedcrypto 2>/dev/null)
MBEDTLS_FALLBACK_CFLAGS :=
MBEDTLS_FALLBACK_LIBS   :=

ifeq ($(UNAME_S),Darwin)
HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null)
ifneq ($(HOMEBREW_PREFIX),)
MBEDTLS_BREW_PREFIX := $(HOMEBREW_PREFIX)/opt/mbedtls
ifneq ($(wildcard $(MBEDTLS_BREW_PREFIX)/include/mbedtls/ssl.h),)
MBEDTLS_FALLBACK_CFLAGS := -I$(MBEDTLS_BREW_PREFIX)/include
MBEDTLS_FALLBACK_LIBS   := -L$(MBEDTLS_BREW_PREFIX)/lib -lmbedtls -lmbedx509 -lmbedcrypto
endif
endif
endif

MBEDTLS_CFLAGS ?= $(if $(strip $(MBEDTLS_PKG_CFLAGS)),$(MBEDTLS_PKG_CFLAGS),$(MBEDTLS_FALLBACK_CFLAGS))
MBEDTLS_LIBS ?= $(if $(strip $(MBEDTLS_PKG_LIBS)),$(MBEDTLS_PKG_LIBS),$(if $(strip $(MBEDTLS_FALLBACK_LIBS)),$(MBEDTLS_FALLBACK_LIBS),-lmbedtls -lmbedx509 -lmbedcrypto))

CXX ?= g++

CXXFLAGS := -std=c++17 -fno-exceptions -fno-rtti \
            -Wall -Wextra -Wpedantic -Werror \
            -Wshadow -Wconversion -Wsign-conversion \
            -Wcast-align -Wformat=2 -Wnull-dereference \
            -Wdouble-promotion -Wno-unknown-pragmas \
            -I$(ME_ROOT)/src $(MBEDTLS_CFLAGS) -g

ME_LIB := $(ME_ROOT)/build/libmessageengine.a

LDFLAGS := -L$(ME_ROOT)/build -lmessageengine -lpthread $(MBEDTLS_LIBS)

BUILD_DIR := build
OBJS := $(BUILD_DIR)/ChaosLabSidecar.o $(BUILD_DIR)/ChaosLabSidecarHttp.o
OUT := $(BUILD_DIR)/chaos_lab_sidecar

.PHONY: all clean

all: $(OUT)

$(ME_LIB):
ifeq ($(ME_SOURCE),live)
	@MESSAGE_ENGINE="$(MESSAGE_ENGINE_ABS)" bash "$(BUILD_ME_LIB)"
else
	@test -f "$(ME_LIB)" || (echo "Missing vendored library: $(ME_LIB)" && exit 1)
endif

$(BUILD_DIR)/ChaosLabSidecar.o: ChaosLabSidecar.cpp ChaosLabSidecarHttp.hpp $(ME_LIB)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ ChaosLabSidecar.cpp

$(BUILD_DIR)/ChaosLabSidecarHttp.o: ChaosLabSidecarHttp.cpp ChaosLabSidecarHttp.hpp $(ME_LIB)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c -o $@ ChaosLabSidecarHttp.cpp

$(OUT): $(OBJS) $(ME_LIB)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
