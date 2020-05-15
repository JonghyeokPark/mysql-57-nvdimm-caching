#!/bin/bash

BASE_DIR=`pwd -P`
BUILD_DIR=$BASE_DIR/bld

# Make a directory for build
if [ ! -d "$BUILD_DIR" ]; then
    echo "Make a directory for build"
    mkdir bld
fi

#cd $BASE_DIR
cd $BUILD_DIR
rm -rf CMakeCache.txt
sudo rm -rf CMakeFiles/*

# Build and install the source code
if [ "$1" = "--origin" ]; then
    # No caching
    BUILD_FLAGS=""
elif [ "$1" = "--nc-ol" ]; then
    # Cache New-Orders and Order-Line pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL"
elif [ "$1" = "--nc-st" ]; then
    # Cache New-Orders and Stock pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_ST"
elif [ "$1" = "--nc-ol-st" ]; then
    # Cache New-Orders, Order-Line and Stock pages
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL -DUNIV_NVDIMM_CACHE_ST"
elif [ "$1" = "--mtr" ]; then
 		# Cache New-Orders, Order-Line with mtr-logging enabled
		BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL -DUNIV_LOG_HEADER"
else
    # Cache New-Orders and Order-Line pages (default)
    BUILD_FLAGS="-DUNIV_NVDIMM_CACHE -DUNIV_NVDIMM_CACHE_NO -DUNIV_NVDIMM_CACHE_OL"
fi

echo "Start build using $BUILD_FLAGS"

cmake .. -DWITH_DEBUG=0 -DCMAKE_C_FLAGS="$BUILD_FLAGS" -DCMAKE_CXX_FLAGS="$BUILD_FLAGS" \
-DDOWNLOAD_BOOST=ON -DWITH_BOOST=$BASE_DIR/boost -DENABLED_LOCAL_INFILE=1 -DCMAKE_INSTALL_PREFIX=$BUILD_DIR \
-DWITH_RAPID=OFF
#-DWITH_INNOBASE_STORAGE_ENGINE=1 -DWITH_ARCHIVE_STORAGE_ENGINE=1

make -j8
sudo make install
