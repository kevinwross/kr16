set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc-11)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++-11)

set(CMAKE_SYSROOT /opt/arm-rootfs)
set(CMAKE_FIND_ROOT_PATH /opt/arm-rootfs)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_C_FLAGS_INIT
    "-isystem /opt/arm-rootfs/usr/include/aarch64-linux-gnu -isystem /opt/arm-rootfs/usr/include")
set(CMAKE_CXX_FLAGS_INIT
    "-isystem /opt/arm-rootfs/usr/include/c++/11 -isystem /opt/arm-rootfs/usr/include/aarch64-linux-gnu/c++/11 -isystem /opt/arm-rootfs/usr/include/aarch64-linux-gnu -isystem /opt/arm-rootfs/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-B/opt/arm-rootfs/usr/lib/aarch64-linux-gnu -L/opt/arm-rootfs/usr/lib/aarch64-linux-gnu -L/opt/arm-rootfs/lib/aarch64-linux-gnu -Wl,-rpath-link,/opt/arm-rootfs/lib/aarch64-linux-gnu")
