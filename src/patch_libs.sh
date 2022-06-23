#!/bin/sh

mv gcc/libgcc_s.so.1 gcc/libgcc_s.so.1.bak
sed -e 's|GCC_[0-9]\.[0-9]|GCC_9.9|g' \
  -e 's|GCC_[0-9]\.[0-9]\.[0-9]|GCC_9.9.9|g' \
  -e 's|GCC_[0-9][0-9]\.[0-9]\.[0-9]|GCC_99.9.9|g' \
  gcc/libgcc_s.so.1.bak > gcc/libgcc_s.so.1

mv cxx/libstdc++.so.6 cxx/libstdc++.so.6.bak
sed -e 's|GLIBCXX_[0-9]\.[0-9]|GLIBCXX_9.9|g' \
  -e 's|GLIBCXX_[0-9]\.[0-9]\.[0-9]|GLIBCXX_9.9.9|g' \
  -e 's|GLIBCXX_[0-9]\.[0-9]\.[0-9][0-9]|GLIBCXX_9.9.99|g' \
  cxx/libstdc++.so.6.bak > cxx/libstdc++.so.6


