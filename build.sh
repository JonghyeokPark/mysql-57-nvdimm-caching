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
echo "Start build"
cmake -DWITH_DEBUG=0 -DCMAKE_C_FLAGS="-DUNIV_NVDIMM_CACHE" -DCMAKE_CXX_FLAGS="-DUNIV_NVDIMM_CACHE" -DDOWNLOAD_BOOST=ON -DWITH_BOOST=$BASE_DIR/boost -DCMAKE_INSTALL_PREFIX=$BUILD_DIR
make -j8 install
