cmake_minimum_required(VERSION 3.19)
project(krkr2plugin)

add_library(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PUBLIC
    scriptsEx.cpp
    win32dialog.cpp
    dirlist.cpp
    csvParser.cpp
    layerExMovie.cpp
    varfile.cpp
    saveStruct.cpp
    getabout.cpp
    addFont.cpp
    wutcwf.cpp
    windowEx.cpp
    getSample.cpp
    layerExPerspective.cpp
    fftgraph.cpp
    LayerExBase.cpp
    xp3filter.cpp
    extrans.cpp
    textrender.cpp
    alphamovie.cpp
    layerExBTOA.cpp
    layerExImage.cpp
    layerExRaster.cpp
    layerExAreaAverage.cpp
    layerExLongExposure.cpp
    krkrgles.cpp
    drawDeviceD2DCompat.cpp
    wfBasicEffectCompat.cpp
    wfTypicalDSPCompat.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${KRKR2CORE_PATH}/
    ${KRKR2CORE_PATH}/base
    ${KRKR2CORE_PATH}/base/win32
    ${KRKR2CORE_PATH}/environ
    ${KRKR2CORE_PATH}/environ/win32
    ${KRKR2CORE_PATH}/environ/android
    ${KRKR2CORE_PATH}/environ/sdl
    ${KRKR2CORE_PATH}/msg
    ${KRKR2CORE_PATH}/msg/win32
    ${KRKR2CORE_PATH}/extension
    ${KRKR2CORE_PATH}/sound
    ${KRKR2CORE_PATH}/sound/win32
    ${KRKR2CORE_PATH}/tjs2
    ${KRKR2CORE_PATH}/utils
    ${KRKR2CORE_PATH}/utils/win32
    ${KRKR2CORE_PATH}/visual
    ${KRKR2CORE_PATH}/visual/ARM
    ${KRKR2CORE_PATH}/visual/win32
    ${KRKR2CORE_PATH}/visual/RenderScript/rs
)

add_definitions(-D__STDC_CONSTANT_MACROS -DTJS_TEXT_OUT_CRLF)

# ---------------------------------------------------------------------------
# Live2D Cubism SDK
# ---------------------------------------------------------------------------
set(CUBISM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/cubism)
set(CUBISM_CORE_DIR ${CUBISM_DIR}/Core)
set(CUBISM_FW_DIR ${CUBISM_DIR}/Framework)
set(CUBISM_FW_SRC_DIR ${CUBISM_FW_DIR}/src)

if (EXISTS ${CUBISM_FW_DIR} AND NOT IOS)
    target_sources(${PROJECT_NAME} PUBLIC krkrlive2d.cpp)
    file(GLOB_RECURSE CUBISM_FW_SOURCES
        ${CUBISM_FW_DIR}/*.cpp
    )
    # Exclude non-GLES2 renderers and unnecessary directories
    list(FILTER CUBISM_FW_SOURCES EXCLUDE REGEX "/D3D9/|/D3D11/|/Vulkan/|/Metal/|/Cocos2d/|/N3DS/|/OpenGL3/")

    add_library(CubismFramework STATIC ${CUBISM_FW_SOURCES})
    target_include_directories(CubismFramework PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_FW_SRC_DIR}
        ${CUBISM_CORE_DIR}/include
    )
    target_compile_definitions(CubismFramework PUBLIC CSM_TARGET_ANGLE_ES2)
    # Automatically inject GLES3 headers to avoid manual patches in the SDK
    if(APPLE OR WIN32)
        if(MSVC)
            target_compile_options(CubismFramework PRIVATE "/FI\"GLES3/gl3.h\"" "/FI\"GLES2/gl2ext.h\"")
        else()
            target_compile_options(CubismFramework PRIVATE "-includeGLES3/gl3.h" "-includeGLES2/gl2ext.h")
        endif()
    endif()
    
    # Propagate GLES2 include paths to CubismFramework
    if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
        target_include_directories(CubismFramework PRIVATE
            ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
        )
    endif()

    if(APPLE AND NOT IOS)
        if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
            set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/x86_64/libLive2DCubismCore.a)
        else()
            set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/macos/arm64/libLive2DCubismCore.a)
        endif()
    elseif(ANDROID)
        set(CUBISM_CORE_ARCH ${CMAKE_ANDROID_ARCH_ABI})
        set(CUBISM_CORE_LIB ${CUBISM_CORE_DIR}/lib/android/${CUBISM_CORE_ARCH}/libLive2DCubismCore.a)
        target_link_libraries(CubismFramework PRIVATE EGL GLESv2)
    endif()

    if(EXISTS ${CUBISM_CORE_LIB})
        target_link_libraries(CubismFramework PUBLIC ${CUBISM_CORE_LIB})
    endif()

    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CUBISM_FW_DIR}
        ${CUBISM_FW_SRC_DIR}
        ${CUBISM_CORE_DIR}/include
    )
    target_compile_definitions(${PROJECT_NAME} PUBLIC CSM_TARGET_ANGLE_ES2)
    target_compile_definitions(${PROJECT_NAME} PUBLIC KRKR_ENABLE_LIVE2D)
    target_link_libraries(${PROJECT_NAME} PUBLIC CubismFramework)
else()
    message(WARNING "Live2D SDK not found in ${CUBISM_DIR} (or disabled). Skipping live2d plugin.")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${KRKR2PLUGIN_PATH} ${KRKR2PLUGIN_PATH}/simplebinder)
target_link_libraries(${PROJECT_NAME} PUBLIC krkr2core)

add_subdirectory(psdfile)
add_subdirectory(psbfile)
add_subdirectory(layerex_draw)
add_subdirectory(motionplayer)
add_subdirectory(kagparserex)

#add_subdirectory(json)
add_subdirectory(fstat)
#add_subdirectory(steam)
#add_subdirectory(DrawDeviceForSteam)
target_link_libraries(${PROJECT_NAME} PUBLIC
        psdfile
        psbfile
        layerExDraw
        motionplayer
        kagparserex
        fstat
#        json steam DrawDeviceForSteam
)

find_package(fmt CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE fmt::fmt)

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE spdlog::spdlog)
