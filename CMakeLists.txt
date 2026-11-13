cmake_minimum_required(VERSION 3.20)
project(ShowFract LANGUAGES CXX)

find_package(OpenMP REQUIRED)

add_executable(ShowFract WIN32
    src/main.cpp
    src/fractal.cpp
    src/fractal_avx2.cpp
)
target_compile_features(ShowFract PRIVATE cxx_std_17)
target_compile_definitions(ShowFract PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(ShowFract PRIVATE /W4 /fp:precise /utf-8)
set_source_files_properties(src/fractal_avx2.cpp PROPERTIES COMPILE_OPTIONS /arch:AVX2)
target_link_libraries(ShowFract PRIVATE OpenMP::OpenMP_CXX gdi32 user32 shell32)
