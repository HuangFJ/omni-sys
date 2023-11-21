UNAME := $(shell uname)
UNAME_P := $(shell uname -p)
PWD := $(shell pwd)

CXX = clang++ -std=c++17 -g -O2 -Wall

ifeq ($(UNAME), Linux)
	AR = ar crsT
else ifeq ($(UNAME), Darwin)
	AR = libtool -static -o
endif

AR = armerge -o 

DYNAMIC = -DHAVE_CONFIG_H

INCLUDE = -I$(PWD)/omnicore/src \
	-I$(PWD)/omnicore/src/config \
	-I$(PWD)/omnicore/src/leveldb/include \
	-I$(PWD)/omnicore/src/univalue/include \
	-I$(PWD)/omnicore/src/secp256k1/include \
	-I$(PWD)/omnicore/src/crc32c/include \
	-I$(PWD)/jsonrpccxx

LIBDIR = -L$(PWD)/omnicore/src \
	-L$(PWD)/omnicore/src/.libs \
	-L$(PWD)/omnicore/src/crypto/.libs \
	-L$(PWD)/omnicore/src/leveldb/.libs \
	-L$(PWD)/omnicore/src/crc32c/.libs \
	-L$(PWD)/omnicore/src/secp256k1/.libs
	

ifneq ($(filter arm% aarch64,$(UNAME_P)),)
	LIBBITCOIN_CRYPTO_a = $(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_base.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_arm_shani.a

	LIBCRC32C_a = $(PWD)/omnicore/src/crc32c/.libs/libcrc32c.a \
		$(PWD)/omnicore/src/crc32c/.libs/libcrc32c_arm_crc.a

	LIBBITCOIN_CRYPTO = -lbitcoin_crypto_base \
		-lbitcoin_crypto_arm_shani

	LIBCRC32C = -lcrc32c \
		-lcrc32c_arm_crc
endif

ifneq ($(filter x86_64,$(UNAME_P)),)
	LIBBITCOIN_CRYPTO_a = $(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_base.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_sse41.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_avx2.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_x86_shani.a

	LIBCRC32C_a = $(PWD)/omnicore/src/crc32c/.libs/libcrc32c.a \
		$(PWD)/omnicore/src/crc32c/.libs/libcrc32c_sse42.a

	LIBBITCOIN_CRYPTO = -lbitcoin_crypto_base \
		-lbitcoin_crypto_sse41 \
		-lbitcoin_crypto_avx2 \
		-lbitcoin_crypto_x86_shani

	LIBCRC32C =	-lcrc32c \
		-lcrc32c_sse42
endif

LIBS = -lbitcoin_node \
	-lbitcoin_common \
	-lbitcoin_util \
	-lunivalue \
	-lbitcoin_consensus \
	$(LIBBITCOIN_CRYPTO) \
	-lleveldb \
	$(LIBCRC32C) \
	-lmemenv \
	-lsecp256k1 \
	-lbitcoinconsensus

LIBS_a = $(PWD)/omnicore/src/libbitcoin_node.a \
	$(PWD)/omnicore/src/libbitcoin_common.a \
	$(PWD)/omnicore/src/libbitcoin_util.a \
	$(PWD)/omnicore/src/.libs/libunivalue.a \
	$(PWD)/omnicore/src/libbitcoin_consensus.a \
	$(LIBBITCOIN_CRYPTO_a) \
	$(PWD)/omnicore/src/leveldb/.libs/libleveldb.a \
	$(LIBCRC32C_a) \
	$(PWD)/omnicore/src/leveldb/.libs/libmemenv.a \
	$(PWD)/omnicore/src/secp256k1/.libs/libsecp256k1.a \
	$(PWD)/omnicore/src/.libs/libbitcoinconsensus.a

# LIBS += -lcpp-httplib

objects:
	$(CXX) -c $(DYNAMIC) $(INCLUDE) src/omni.cpp -o src/omni.o
	$(CXX) -c $(DYNAMIC) $(INCLUDE) src/test.cpp -o src/test.o

# lib: objects
# 	mkdir -p src/.libs
# 	cd src/.libs \
# 	&& for lib_a in $(LIBS_a); do \
# 		ar x $$lib_a; \
# 	done
# 	ar rcs src/libomni.a src/omni.o src/.libs/*.o

# test: objects
# 	$(CXX) $(DYNAMIC) $(INCLUDE) $(LIBDIR) src/test.o src/omni.o $(LIBS) -o src/test.out

omnicore/src/config/bitcoin-config.h:
	cd omnicore && ./autogen.sh
	cd omnicore && ./configure CXX=clang++ CC=clang --disable-wallet --disable-zmq --disable-bench --disable-tests --disable-fuzz-binary --without-gui --without-miniupnpc --without-natpmp

libomnicore: omnicore/src/config/bitcoin-config.h
	rm -f src/libomnicore.a
	make -C omnicore -j8
	$(AR) src/libomnicore.a $(LIBS_a)

test: objects src/libomnicore.a
	$(CXX) src/test.o src/omni.o src/libomnicore.a -o src/test.out
	./src/test.out

clean:
	rm -rf src/*.o src/*.a src/*.out src/.libs
