# The problem

Some projects require newer C++ standards to build them. To keep the glibc dependency low you can
build a newer GCC version on an older distro and use it to compile the project.
This project however will now require a newer version of the `libstdc++.so.6` library than available on that distro.
Bundling `libstdc++.so.6` however will in most cases break compatibility with distros that have a newer library
version installed into their system than the bundled one. So blindly bundling the library is not reliable.

By the way, while this is primarily an issue with `libstdc++.so.6` in some rare cases this might also occur with `libgcc_s.so.1`.
That's because both libraries are part of GCC.


# The solution

You would have to know the library version of the host system and decide whether to use a bundled library or not before the
application is started. This is exactly what the patched AppRun binary does.
It will search for `usr/optional/libstdc++/libstdc++.so.6` and `usr/optional/libgcc_s/libgcc_s.so.1` inside the AppImage or AppDir.
If found it will compare their internal versions with the ones found on the system and prepend their paths to `LD_LIBRARY_PATH` if necessary.

You should also put `exec.so` into `usr/optional`. This exec.so library is intended to restore the environment of the AppImage to its parent.
This is done to avoid library clashing of bundled libraries with external processes. e.g when running the web browser

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
