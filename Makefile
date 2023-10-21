UNAME_P := $(shell uname -p)
PWD := $(shell pwd)

CXX = clang++ -std=c++17 -g -O2 -Wall

DYNAMIC = -DHAVE_CONFIG_H \
	# -DFETCH_REMOTE_TX

INCLUDE = -I$(PWD)/omnicore/src \
	-I$(PWD)/omnicore/src/config \
	-I$(PWD)/omnicore/src/leveldb/include \
	-I$(PWD)/omnicore/src/univalue/include \
	-I$(PWD)/omnicore/src/secp256k1/include \
	-I$(PWD)/omnicore/src/crc32c/include \
	-I$(PWD)/jsonrpccxx

LIBDIR = -L$(PWD)/omnicore/src \
	-L$(PWD)/omnicore/src/leveldb/.libs \
	-L$(PWD)/omnicore/src/crypto/.libs \
	-L$(PWD)/omnicore/src/crc32c/.libs \
	-L$(PWD)/omnicore/src/secp256k1/.libs \
	-L$(PWD)/omnicore/src/.libs

LIBS = -lcpp-httplib \
	-Wl,--start-group \
	-lbitcoin_node \
	-lbitcoin_consensus \
	-lbitcoin_util \
	-lbitcoin_common \
	-lbitcoin_crypto_base \
	-lleveldb \
	-lmemenv \
	-lcrc32c \
	-lsecp256k1 \
	-lunivalue

LIBS_a = $(PWD)/omnicore/src/leveldb/.libs/libleveldb.a \
	$(PWD)/omnicore/src/leveldb/.libs/libmemenv.a \
	$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_base.a \
	$(PWD)/omnicore/src/crc32c/.libs/libcrc32c.a \
	$(PWD)/omnicore/src/libbitcoin_common.a \
	$(PWD)/omnicore/src/libbitcoin_util.a \
	$(PWD)/omnicore/src/secp256k1/.libs/libsecp256k1.a \
	$(PWD)/omnicore/src/libbitcoin_consensus.a \
	$(PWD)/omnicore/src/libbitcoin_node.a \
	$(PWD)/omnicore/src/.libs/libunivalue.a

ifneq ($(filter arm% aarch64,$(UNAME_P)),)
	LIBS_a += $(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_arm_shani.a \
		$(PWD)/omnicore/src/crc32c/.libs/libcrc32c_arm_crc.a
	LIBS += -lbitcoin_crypto_arm_shani \
		-lcrc32c_arm_crc
endif

ifneq ($(filter x86_64,$(UNAME_P)),)
	LIBS_a += $(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_x86_shani.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_avx2.a \
		$(PWD)/omnicore/src/crypto/.libs/libbitcoin_crypto_sse41.a \
		$(PWD)/omnicore/src/crc32c/.libs/libcrc32c_sse42.a
	LIBS += -lbitcoin_crypto_x86_shani \
		-lbitcoin_crypto_avx2 \
		-lbitcoin_crypto_sse41 \
		-lcrc32c_sse42
endif

LIBS += -Wl,--end-group

objects:
	$(CXX) -c $(DYNAMIC) $(INCLUDE) src/omni.cpp -o src/omni.o

lib: objects
	mkdir -p src/.libs
	cd src/.libs \
	&& for lib_a in $(LIBS_a); do \
		ar x $$lib_a; \
	done
	ar rcs src/libomni.a src/omni.o src/.libs/*.o

out: objects
	$(CXX) $(DYNAMIC) $(INCLUDE) $(LIBDIR) src/omni.o $(LIBS) -o src/omni.out

clean:
	rm -rf src/*.o src/*.a src/omni.out src/.libs