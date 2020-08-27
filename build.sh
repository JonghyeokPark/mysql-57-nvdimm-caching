#!/bin/bash

BASE_DIR=`pwd -P`
BUILD_DIR=$BASE_DIR/bld
PASSWD="sudo-passwd"

# Make a directory for build
if [ ! -d "$BUILD_DIR" ]; then
    echo "Make a directory for build"
    mkdir bld
fi

cd $BUILD_DIR

rm -rf CMakeCache.txt
echo $PASSWD | sudo -S rm -rf CMakeFiles/*

# Build and install the source code
if [ "$1" = "--origin" ]; then
    # No caching
    BUILD_FLAGS=""
elif [ "$1" = "--origin-monitor" ]; then
    # No caching but monitor the flush status
    BUILD_FLAGS="-DUNIV_FLUSH_MONITOR"
elif [ "$1" = "--nc" ]; then
    # Cache New-Orders and Order-Line pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE"
elif [ "$1" = "--nc-st" ]; then
    # Cache New-Orders, Order-Line and Stock pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_ST"
elif [ "$1" = "--nc-st-od" ]; then
    # Cache New-Orders, Order-Line, Stock and Orders pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_ST -DUNIV_NVDIMM_CACHE_OD"
elif [ "$1" = "--mtr" ]; then
    # Cache New-Orders, Order-Line, Stock and Orders pages with mtr-logging enabled
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_ST -DUNIV_NVDIMM_CACHE_OD -DUNIV_LOG_HEADER"
else
    # Cache New-Orders and Order-Line pages (default)
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE"
fi

echo "Start build using $BUILD_FLAGS"

cd $BASE_DIR

cmake -DWITH_DEBUG=0 -DCMAKE_C_FLAGS="$BUILD_FLAGS" -DCMAKE_CXX_FLAGS="$BUILD_FLAGS" \
-DDOWNLOAD_BOOST=ON -DWITH_BOOST=$BASE_DIR/boost -DENABLED_LOCAL_INFILE=1 \
-DCMAKE_INSTALL_PREFIX=$BUILD_DIR

make -j8
echo $PASSWD | sudo -S make install
