#!/bin/bash

find src -regextype egrep -regex '.*(cc|h)' | xargs clang-format -style=file -i

echo 'code formated'
