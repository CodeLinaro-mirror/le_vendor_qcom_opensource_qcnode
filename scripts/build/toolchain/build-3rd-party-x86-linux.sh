#!/bin/bash
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Build third-party libraries for the x86-linux native target.
# No cross-compile flags, no sysroot.

set -xe

homedir=$(dirname $0)
homedir=$(realpath $homedir)

# Build directory
workdir=$1/third_party

# Install / runtime directory
destdir=$2/opt/qcnode

mkdir -p $destdir || exit 1
mkdir -p $workdir && cd $workdir || exit 1

# No cross-compile flags — use the host toolchain with system defaults
unset CFLAGS CXXFLAGS LDFLAGS

if [ "$ENABLE_TINYVIZ" == "ON" ] ; then

# build and install SDL2 for TinyViz (native x86, no cross-compile flags)
cd $THIRD_PARTY_DIR
if [ ! -d $destdir/include/SDL2 ]; then
    if [ ! -f SDL2-2.0.14.tar.gz ]; then
        echo "Package SDL2-2.0.14.tar.gz not found under $THIRD_PARTY_DIR"
    else
        tar -zxf SDL2-2.0.14.tar.gz -C $workdir
        cd $workdir/SDL2-2.0.14
        ./configure LDFLAGS="-lwayland-client" \
          --prefix=$destdir \
          --enable-esd=no --enable-pulseaudio=no \
          --enable-dbus=no
        make -j16
        make install
    fi
fi

# build and install SDL2_gfx
cd $THIRD_PARTY_DIR
if [ ! -f $destdir/lib/libSDL2_gfx.so ]; then
    if [ ! -f SDL2_gfx-1.0.4.tar.gz ]; then
        echo "Package SDL2_gfx-1.0.4.tar.gz not found under $THIRD_PARTY_DIR"
    else
        tar -zxf SDL2_gfx-1.0.4.tar.gz -C $workdir
        cd $workdir/SDL2_gfx-1.0.4
        cp /usr/share/libtool/build-aux/config.sub .
        cp /usr/share/libtool/build-aux/config.guess .
        ./configure \
          --prefix=$destdir \
          --with-sdl-prefix=$destdir --enable-mmx=no
        make -j16
        make install
    fi
fi

# build and install SDL2_ttf
cd $THIRD_PARTY_DIR
if [ ! -f $destdir/lib/libSDL2_ttf.so ]; then
    if [ ! -f SDL2_ttf-2.0.15.tar.gz ]; then
        echo "Package SDL2_ttf-2.0.15.tar.gz not found under $THIRD_PARTY_DIR"
    else
        tar -zxf SDL2_ttf-2.0.15.tar.gz -C $workdir
        cd $workdir/SDL2_ttf-2.0.15/external/freetype-2.9.1
        sh ./autogen.sh
        ./configure \
          --prefix=$destdir --with-png=no --with-harfbuzz=no
        make -j16
        make install
        cd $workdir/SDL2_ttf-2.0.15
        export PKG_CONFIG_PATH=$destdir/lib/pkgconfig:$PKG_CONFIG_PATH
        ./configure \
          --prefix=$destdir \
          --with-sdl-prefix=$destdir \
          --with-ft-prefix=$destdir
        make -j16
        make install
    fi
fi

fi # ENABLE_TINYVIZ

# Build and install nlohmann/json
cd $THIRD_PARTY_DIR
if [ ! -d $destdir/include/nlohmann ]; then
    if [ ! -f json.tar.gz ]; then
        echo "Package json.tar.gz not found under $THIRD_PARTY_DIR"
        exit -1
    else
        tar -xf json.tar.gz -C $workdir
        cd $workdir/json*
        mkdir -p build && cd build
        cmake \
        -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX=$destdir \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        -DJSON_BuildTests=OFF \
        ..
        make
        make install
    fi
fi

# Build and install googletest (no QNX patch for x86-linux)
cd $THIRD_PARTY_DIR
if [ ! -d $destdir/include/gtest ]; then
    if [ ! -f googletest-1.10.0.tar.gz ]; then
        echo "Package googletest-1.10.0.tar.gz not found under $THIRD_PARTY_DIR"
        exit -1
    else
        tar -xf googletest-1.10.0.tar.gz -C $workdir
        cd $workdir/googletest-release-1.10.0
        mkdir -p build && cd build
        cmake \
        -DCMAKE_TOOLCHAIN_FILE=$CMAKE_TOOLCHAIN_FILE \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_INSTALL_PREFIX=$destdir \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        ..
        make
        make install
    fi
fi

