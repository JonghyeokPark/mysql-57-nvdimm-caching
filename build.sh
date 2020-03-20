#!/bin/bash

BASE_DIR=/home/mijin/mysql-57-nvdimm-caching
BUILD_DIR=$BASE_DIR

# Make a directory for build
#if [ ! -d "$BUILD_DIR" ]; then
#    echo "Make a directory for build"
#    mkdir bld
#fi

cd $BUILD_DIR

# Build and install the source code
echo "Start build"
cmake -DWITH_DEBUG=0 -DCMAKE_C_FLAGS="-DUNIV_NVDIMM_CACHE" -DCMAKE_CXX_FLAGS="-DUNIV_NVDIMM_CACHE" -DDOWNLOAD_BOOST=ON -DWITH_BOOST=$BASE_DIR/boost
make -j8
sudo make install
