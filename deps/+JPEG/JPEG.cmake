if(MSVC)
    # Match the application's /MD runtime even when linking libjpeg statically.
    # libjpeg-turbo defaults to /MT, causing LNK4098 in the production DLL.
    set(_jpeg_runtime_args -DWITH_CRT_DLL=ON)
endif()

add_cmake_project(JPEG
    URL https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.1.0.zip
    URL_HASH SHA256=2ddfaadf8b660050ff066a03833416bf8500624f014877b80eff16e799f68e81
    CMAKE_ARGS
        -DENABLE_SHARED=OFF
        -DENABLE_STATIC=ON
        ${_jpeg_runtime_args}
)

set(DEP_JPEG_DEPENDS ZLIB)
