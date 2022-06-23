# The problem

Some projects require newer C++ standards to build them. To keep the glibc
dependency low you can build a newer GCC version on an older distro and use it
to compile the project. This project however will now require a newer version of
the `libstdc++.so.6` library than available on that distro.
Blindly bundling `libstdc++.so.6` however will in most cases break compatibility
with distros that have a newer library version installed into their system than
the bundled one.

By the way, while this is primarily an issue with `libstdc++.so.6` in some cases
this might also occur with `libgcc_s.so.1`. That's because both libraries are
part of GCC.


# The solution

You would have to know the library version of the host system and decide whether
to use a bundled library or not before the application is started. This is
exactly what the `checkrt` tool does. It will search for a bundled `libstdc++.so.6`
and `libgcc_s.so.1` library inside the AppImage or AppDir. If found it will
compare their internal versions with the ones found on the system (using dlopen())
and prepend their paths to `LD_LIBRARY_PATH` if necessary.


# Usage

Compile with `make -C src`. Inside your AppDir create the directory `usr/optional/`
and put the binary `checkrt` inside there. Execute `usr/optional/checkrt --copy-libraries`
to bundle your system's `libstdc++.so.6` and `libgcc_s.so.1` libraries.
Use the provided AppRun script or write/modify your own. `checkrt` will either
return a path that can be added to `LD_LIBRARY_PATH` or an empty string.
Error messages, help and verbosity will always be returned to stderr.


# exec.so

You might also want to bundle `exec.so` inside `usr/optional/`. This library is
intended to restore the environment of the AppImage to its parent. This is done
to avoid library clashing of bundled libraries with external processes, e.g. when
running the web browser.

The intended usage is as follows:

1. This library is injected to the dynamic loader through LD_PRELOAD
   automatically in AppRun **only** if `usr/optional/exec.so` exists: 
   e.g `LD_PRELOAD=$APPDIR/usr/optional/exec.so My.AppImage`

2. This library will intercept calls to new processes and will detect whether
   those calls are for binaries within the AppImage bundle or external ones.

3. In case it's an internal process, it will not change anything.
   In case it's an external process, it will restore the environment of
   the AppImage parent by reading `/proc/[pid]/environ`.
   This is the conservative approach taken.

