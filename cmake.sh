#!/bin/bash

cmake .. -DCMAKE_BUILD_TYPE=Debug -GNinja -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-fcolor-diagnostics
