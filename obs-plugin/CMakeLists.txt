# DirectPipe OBS Source Plugin

# Try to find OBS Studio SDK
# Users should set OBS_SOURCE_DIR or install libobs
find_path(OBS_INCLUDE_DIR
    NAMES obs-module.h obs/obs-module.h
    PATHS
        "${OBS_SOURCE_DIR}/libobs"
        "${OBS_SOURCE_DIR}/UI/obs-frontend-api"
        "$ENV{OBS_DIR}/include"
        "$ENV{OBS_DIR}/include/obs"
    PATH_SUFFIXES obs
)

find_library(OBS_LIB
    NAMES obs libobs
    PATHS
        "${OBS_SOURCE_DIR}/build/libobs"
        "$ENV{OBS_DIR}/lib"
        "$ENV{OBS_DIR}/bin/64bit"
)

if(NOT OBS_INCLUDE_DIR OR NOT OBS_LIB)
    message(WARNING "OBS SDK not found. Skipping OBS plugin build. "
                    "Set OBS_SOURCE_DIR or OBS_DIR to the OBS Studio source/install directory.")
    return()
endif()

add_library(obs-directpipe-source MODULE
    src/plugin-main.c
    src/directpipe-source.c
    src/directpipe-source.h
    src/shm-reader.c
    src/shm-reader.h
)

target_include_directories(obs-directpipe-source PRIVATE
    ${OBS_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/core/include
)

target_link_libraries(obs-directpipe-source PRIVATE
    ${OBS_LIB}
    directpipe-core
)

if(WIN32)
    target_link_libraries(obs-directpipe-source PRIVATE kernel32)
endif()

# Remove 'lib' prefix on all platforms for OBS plugin naming
set_target_properties(obs-directpipe-source PROPERTIES PREFIX "")

# Install to OBS plugin directory
if(WIN32)
    set(OBS_PLUGIN_DIR "$ENV{APPDATA}/obs-studio/plugins/obs-directpipe-source/bin/64bit"
        CACHE PATH "OBS Plugin install directory")
    set(OBS_DATA_DIR "$ENV{APPDATA}/obs-studio/plugins/obs-directpipe-source/data"
        CACHE PATH "OBS Plugin data install directory")
else()
    set(OBS_PLUGIN_DIR "$ENV{HOME}/.config/obs-studio/plugins/obs-directpipe-source/bin/64bit"
        CACHE PATH "OBS Plugin install directory")
    set(OBS_DATA_DIR "$ENV{HOME}/.config/obs-studio/plugins/obs-directpipe-source/data"
        CACHE PATH "OBS Plugin data install directory")
endif()

install(TARGETS obs-directpipe-source LIBRARY DESTINATION ${OBS_PLUGIN_DIR})
install(DIRECTORY data/ DESTINATION ${OBS_DATA_DIR})
