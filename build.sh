#!/bin/bash

BASE_DIR=/home/mijin/mysql-5.7.24
BUILD_DIR=$BASE_DIR/bld

# Make a directory for build
if [ ! -d "$BUILD_DIR" ]; then
    echo "Make a directory for build"
    mkdir bld
fi

cd $BASE_DIR

# Build and install the source code
if [ $1 = "--origin" ]; then
    # No caching
    BUILD_FLAGS=""
elif [ $1 = "--nc-ol" ]; then
    # Cache New-Orders and Order-Line pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL"
elif [ $1 = "--nc-st" ]; then
    # Cache New-Orders and Stock pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_ST"
elif [ $1 = "--nc-ol-st" ]; then
    # Cache New-Orders, Order-Line and Stock pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL -DUNIV_NVDIMM_CACHE_ST"
else
    # Cache New-Orders and Order-Line pages (default)
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL"
fi

echo "Start build using $BUILD_FLAGS"

cmake -DWITH_DEBUG=0 -DCMAKE_C_FLAGS="$BUILD_FLAGS" -DCMAKE_CXX_FLAGS="$BUILD_FLAGS" -DDOWNLOAD_BOOST=ON -DWITH_BOOST=$BASE_DIR/boost -DCMAKE_INSTALL_PREFIX=$BUILD_DIR

make -j8
sudo make install
