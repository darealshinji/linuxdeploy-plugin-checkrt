This Makefile will build an example AppImage for testing purpose.
First you need to build a version of GCC that is newer than the default one
on your system and install it to a custom prefix, let's say `$HOME/gcc`.

Then run `make` with the `GCC_PREFIX` variable set to that path:
``` sh
GCC_PREFIX="$HOME/gcc" make
```

Now run `CHECKRT_DEBUG=1 ./example-1-x86_64.AppImage`.
The output should look similar to this:
```
$ CHECKRT_DEBUG=1 ./example-1-x86_64.AppImage
[DEBUG] get_exe_dir: exe directory found at: /tmp/.mount_examplKohcOL/checkrt
[DEBUG] symbol_version: symbol GCC_14.0.0 found in: /tmp/.mount_examplKohcOL/checkrt/gcc/libgcc_s.so.1
[DEBUG] get_system_library_path: libgcc_s.so.1 resolved to: /lib/x86_64-linux-gnu/libgcc_s.so.1
[DEBUG] symbol_version: symbol GCC_14.0.0 found in: /lib/x86_64-linux-gnu/libgcc_s.so.1
[DEBUG] use_bundled_library: use SYSTEM libgcc_s.so.1 library
[DEBUG] symbol_version: symbol GLIBCXX_3.4.34 found in: /tmp/.mount_examplKohcOL/checkrt/cxx/libstdc++.so.6
[DEBUG] get_system_library_path: libstdc++.so.6 resolved to: /lib/x86_64-linux-gnu/libstdc++.so.6
[DEBUG] symbol_version: symbol GLIBCXX_3.4.33 found in: /lib/x86_64-linux-gnu/libstdc++.so.6
[DEBUG] use_bundled_library: use BUNDLED libstdc++.so.6 library
CHECKRT>> LD_LIBRARY_PATH=/tmp/.mount_examplKohcOL/checkrt/cxx:
CHECKRT>> LD_PRELOAD=/tmp/.mount_examplKohcOL/checkrt/exec.so:
hello world
```
You can see that the bundled libraries are newer versions than on the system
and have therefor been prepended to LD_LIBRARY_PATH.

