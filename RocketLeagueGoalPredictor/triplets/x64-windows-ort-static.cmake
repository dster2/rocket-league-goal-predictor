set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

# --- START FIX FOR ONNXRUNTIME ---
# As per onnxruntime tools/python/util/vcpkg_helpers.py
if(PORT MATCHES "onnx")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        "-DONNX_DISABLE_STATIC_REGISTRATION=ON"
    )
endif()
# --- END FIX FOR ONNXRUNTIME ---