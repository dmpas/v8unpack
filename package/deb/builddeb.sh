#!/bin/sh

. /etc/os-release

VERSION=$(./v8unpack -v)~${VERSION_CODENAME}
PACKAGE_ROOT=v8unpack-${VERSION}/

mkdir -p ${PACKAGE_ROOT}usr/local/bin
mkdir -p ${PACKAGE_ROOT}etc/bash_completion.d
mkdir -p ${PACKAGE_ROOT}usr/share/doc/v8unpack
cp ../package/deb/bash_completion.sh ${PACKAGE_ROOT}etc/bash_completion.d/
cp -r ../package/deb/DEBIAN ${PACKAGE_ROOT}DEBIAN
cp ../README.md ${PACKAGE_ROOT}usr/share/doc/v8unpack/
cp ../LICENSE ${PACKAGE_ROOT}usr/share/doc/v8unpack/
cp ../build/v8unpack ${PACKAGE_ROOT}usr/local/bin/

dpkg-deb --build ${PACKAGE_ROOT}
