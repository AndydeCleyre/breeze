set(BREEZE_COMMON_USE_KDE4 ${USE_KDE4})

if (BREEZE_COMMON_USE_KDE4)
    ############ Language and toolchain features
    ############ copied from ECM
    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel" AND NOT WIN32)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")
    endif ()
endif ()

################# dependencies #################
### FFTW
find_package(FFTW REQUIRED)

### Qt/KDE
if (NOT BREEZE_COMMON_USE_KDE4)
    find_package(Qt5 REQUIRED CONFIG COMPONENTS Widgets)
endif ()

################# configuration #################
configure_file(config-breezecommon.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-breezecommon.h )

################# breezestyle target #################
set(breezecommon_LIB_SRCS
    breezeboxshadowhelper.cpp
)

if (BREEZE_COMMON_USE_KDE4)
    kde4_add_library(breezecommon4 SHARED ${breezecommon_LIB_SRCS})

    generate_export_header(breezecommon4
        BASE_NAME breezecommon
        EXPORT_FILE_NAME breezecommon_export.h)

    target_link_libraries(breezecommon4 ${KDE4_KDEUI_LIBS})
    target_link_libraries(breezecommon4 ${FFTW_LIBRARIES})

    set_target_properties(breezecommon4 PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

    install(TARGETS breezecommon4 ${INSTALL_TARGETS_DEFAULT_ARGS})
else ()
    add_library(breezecommon ${breezecommon_LIB_SRCS})

    generate_export_header(breezecommon
        BASE_NAME breezecommon
        EXPORT_FILE_NAME breezecommon_export.h)

    target_link_libraries(breezecommon
        PUBLIC
            Qt5::Core
            Qt5::Gui
        PRIVATE
            ${FFTW_LIBRARIES})

    set_target_properties(breezecommon PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR})

    install(TARGETS breezecommon ${INSTALL_TARGETS_DEFAULT_ARGS})
endif ()
