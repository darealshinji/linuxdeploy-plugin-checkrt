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
CHECKRT>> libstdc++.so.6 system = 3004028
CHECKRT>> libstdc++.so.6 bundle = 3004031
CHECKRT>> libgcc_s.so.1 system = 7000000
CHECKRT>> libgcc_s.so.1 bundle = 13000000
CHECKRT>> LD_LIBRARY_PATH=/tmp/.mount_examplZGsWQg/apprun-hooks/checkrt/gcc:/tmp/.mount_examplZGsWQg/apprun-hooks/checkrt/cxx:
CHECKRT>> LD_PRELOAD=/tmp/.mount_examplZGsWQg/apprun-hooks/checkrt/exec.so:
hello world
```
You can see that the bundled libraries are newer versions than on the system
and have therefor been prepended to LD_LIBRARY_PATH.

If the library versions are equal to or less than on your system the output
should look like this:
```
CHECKRT>> libstdc++.so.6 system = 3004031
CHECKRT>> libstdc++.so.6 bundle = 3004031
CHECKRT>> libgcc_s.so.1 system = 13000000
CHECKRT>> libgcc_s.so.1 bundle = 13000000
CHECKRT>> LD_LIBRARY_PATH=
CHECKRT>> LD_PRELOAD=/tmp/.mount_examplEwjwfp/apprun-hooks/checkrt/exec.so:
hello world
```
