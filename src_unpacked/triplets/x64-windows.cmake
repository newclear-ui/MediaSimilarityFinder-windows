# Overlay triplet: identical to the built-in x64-windows triplet, but installs
# Release dependencies only. The product is built with --config Release, so the
# Debug half of every dependency was pure cost. This also avoids ports whose
# Debug configuration fails on the floating runner image (dav1d on 2026-11-16).
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_BUILD_TYPE release)
