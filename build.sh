#!/bin/bash

mkdir build
cd build
cmake -GNinja ../
ninja
cp ../vec_add.wgsl .
