#!/bin/bash

mkdir build
cd build
cmake -GNinja ../
ninja
cp ../prefix-sum.wgsl .
