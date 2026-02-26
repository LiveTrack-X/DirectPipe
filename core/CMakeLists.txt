# DirectPipe Core Library
# SPSC Lock-free Ring Buffer + Shared Memory IPC

add_library(directpipe-core STATIC
    src/RingBuffer.cpp
    src/SharedMemory.cpp
)

target_include_directories(directpipe-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(directpipe-core PUBLIC cxx_std_17)

# Windows-specific linking
if(WIN32)
    target_link_libraries(directpipe-core PUBLIC kernel32)
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(directpipe-core PRIVATE /W4 /WX)
else()
    target_compile_options(directpipe-core PRIVATE -Wall -Wextra -Wpedantic)
endif()
