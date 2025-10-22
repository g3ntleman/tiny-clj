#!/bin/bash
cd /Users/theisen/Projects/tiny-clj
printf "(list 1 2 3)\n\004" | timeout 5s ./build-release/tiny-clj-repl 2>&1 | tail -20
