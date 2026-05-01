cmake_minimum_required(VERSION 3.16)
project(metal_metrology LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

option(USE_ONNXRUNTIME "Enable ONNX Runtime inference" OFF)
option(USE_MVS "Enable Hikrobot MVS SDK camera backend" ON)
option(USE_HALCON "Enable HALCON calibration backend" ON)

if(NOT OpenCV_DIR)
    set(_opencv_candidates
        "$ENV{OpenCV_DIR}"
        "$ENV{HOME}/projects/opencv_build/opencv-4.9.0/build"
        "$ENV{HOME}/projects/opencv_build/opencv-4.9.0/build/unix-install"
    )

    foreach(_dir IN LISTS _opencv_candidates)
        if(_dir AND EXISTS "${_dir}/OpenCVConfig.cmake")
            set(OpenCV_DIR "${_dir}" CACHE PATH "Path to OpenCVConfig.cmake" FORCE)
            message(STATUS "Auto-detected OpenCV_DIR: ${OpenCV_DIR}")
            break()
        endif()
    endforeach()
endif()

find_package(OpenCV REQUIRED)

set(_opencv_core_lib "")
foreach(_opencv_lib IN LISTS OpenCV_LIBS)
    if(_opencv_lib MATCHES "opencv_core")
        set(_opencv_core_lib "${_opencv_lib}")
        break()
    endif()
endforeach()
if(NOT _opencv_core_lib)
    set(_opencv_core_lib ${OpenCV_LIBS})
endif()

add_library(metal_foundation STATIC
    src/config.cpp
    src/logger.cpp
)

target_include_directories(metal_foundation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(metal_foundation PUBLIC ${_opencv_core_lib})

add_library(metal_core STATIC
    src/camera_provider.cpp
    src/onnx_inferencer.cpp
    src/tracker_ekf.cpp
    src/calibration.cpp
    src/subpixel_caliper.cpp
    src/measurement_uncertainty.cpp
)

target_include_directories(metal_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(metal_core PUBLIC metal_foundation)

add_library(metal_app STATIC
    src/app/pipeline_context.cpp
    src/app/pipeline_runner.cpp
    src/app/pipeline_init.cpp
    src/app/frame_processor.cpp
    src/app/frame_visualizer.cpp
)

target_link_libraries(metal_app PUBLIC metal_core)

add_executable(metal_metrology
    src/main.cpp
)

target_link_libraries(metal_metrology PRIVATE metal_app)

set(_runtime_rpaths "")

if(USE_HALCON)
    target_compile_definitions(metal_core PUBLIC USE_HALCON=1)

    if(NOT DEFINED HALCON_ROOT)
        set(_halcon_roots
            "$ENV{HALCONROOT}"
            "$ENV{HOME}/MVTec/HALCON-25.11-Progress"
            "/opt/halcon"
            "/opt/HALCON"
        )
        foreach(_r IN LISTS _halcon_roots)
            if(_r AND (EXISTS "${_r}/include/HalconCpp.h" OR EXISTS "${_r}/include/halconcpp/HalconCpp.h"))
                set(HALCON_ROOT "${_r}" CACHE PATH "HALCON installation root" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT HALCON_ROOT OR (NOT EXISTS "${HALCON_ROOT}/include/HalconCpp.h" AND NOT EXISTS "${HALCON_ROOT}/include/halconcpp/HalconCpp.h"))
        message(FATAL_ERROR "USE_HALCON=ON 但未找到 HalconCpp.h，请设置 -DHALCON_ROOT=/path/to/halcon")
    endif()

    if(NOT DEFINED HALCON_ARCH)
        if(DEFINED ENV{HALCONARCH} AND NOT "$ENV{HALCONARCH}" STREQUAL "")
            set(HALCON_ARCH "$ENV{HALCONARCH}" CACHE STRING "HALCON architecture" FORCE)
        else()
            set(HALCON_ARCH "x64-linux" CACHE STRING "HALCON architecture" FORCE)
        endif()
    endif()

    set(HALCON_LIB_DIR "${HALCON_ROOT}/lib/${HALCON_ARCH}")
    if(NOT EXISTS "${HALCON_LIB_DIR}")
        message(FATAL_ERROR "未找到 HALCON 库目录: ${HALCON_LIB_DIR}，请检查 HALCON_ARCH")
    endif()

    target_include_directories(metal_core PUBLIC "${HALCON_ROOT}/include")
    target_link_directories(metal_core PUBLIC "${HALCON_LIB_DIR}")
    target_link_libraries(metal_core PRIVATE halconcpp halcon)
    list(APPEND _runtime_rpaths "${HALCON_LIB_DIR}")
endif()

if(USE_MVS)
    if(NOT DEFINED MVS_DIR)
        set(MVS_DIR "/opt/MVS")
    endif()

    if(NOT EXISTS "${MVS_DIR}/include/MvCameraControl.h")
        message(FATAL_ERROR "USE_MVS=ON 但未找到 ${MVS_DIR}/include/MvCameraControl.h，请设置 -DMVS_DIR=/opt/MVS")
    endif()

    target_compile_definitions(metal_core PUBLIC USE_MVS=1)
    target_include_directories(metal_core PUBLIC ${MVS_DIR}/include)
    target_link_directories(metal_core PUBLIC ${MVS_DIR}/lib/64)
    target_link_libraries(metal_core PRIVATE MvCameraControl pthread)
    list(APPEND _runtime_rpaths "${MVS_DIR}/lib/64")
endif()

if(USE_ONNXRUNTIME)
    target_compile_definitions(metal_core PUBLIC USE_ONNXRUNTIME=1)

    if(NOT ONNXRUNTIME_INCLUDE_DIR)
        set(_ort_include_candidates
            "$ENV{ONNXRUNTIME_DIR}/include"
            "${ONNXRUNTIME_DIR}/include"
            "$ENV{HOME}/onnxruntime/include"
        )
        foreach(_d IN LISTS _ort_include_candidates)
            if(_d AND EXISTS "${_d}/onnxruntime/core/session/onnxruntime_cxx_api.h")
                set(ONNXRUNTIME_INCLUDE_DIR "${_d}" CACHE PATH "ONNX Runtime include directory" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT ONNXRUNTIME_LIB_DIR)
        set(_ort_lib_candidates
            "$ENV{ONNXRUNTIME_DIR}/lib"
            "${ONNXRUNTIME_DIR}/lib"
            "$ENV{HOME}/onnxruntime/build/Linux/Release"
            "$ENV{HOME}/miniconda3/envs/yolo/lib/python3.10/site-packages/onnxruntime/capi"
        )
        foreach(_d IN LISTS _ort_lib_candidates)
            if(_d AND (EXISTS "${_d}/libonnxruntime.so" OR EXISTS "${_d}/libonnxruntime.so.1.18.0" OR EXISTS "${_d}/libonnxruntime.so.1.23.2"))
                set(ONNXRUNTIME_LIB_DIR "${_d}" CACHE PATH "ONNX Runtime library directory" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime/core/session/onnxruntime_cxx_api.h")
        message(FATAL_ERROR "未找到 ONNX Runtime 头文件，请设置 -DONNXRUNTIME_INCLUDE_DIR=/path/to/include")
    endif()

    if(NOT ONNXRUNTIME_LIB_DIR)
        message(FATAL_ERROR "未找到 ONNX Runtime 库目录，请设置 -DONNXRUNTIME_LIB_DIR=/path/to/lib")
    endif()

    target_include_directories(metal_core PUBLIC
        ${ONNXRUNTIME_INCLUDE_DIR}
        ${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime/core/session
    )
    find_library(ONNXRUNTIME_LIB onnxruntime PATHS ${ONNXRUNTIME_LIB_DIR} REQUIRED NO_DEFAULT_PATH)
    if(NOT ONNXRUNTIME_LIB)
        message(FATAL_ERROR "未在 ${ONNXRUNTIME_LIB_DIR} 找到 libonnxruntime.so")
    endif()
    target_link_libraries(metal_core PRIVATE ${ONNXRUNTIME_LIB})
    list(APPEND _runtime_rpaths "${ONNXRUNTIME_LIB_DIR}")
endif()

target_link_libraries(metal_core PUBLIC ${OpenCV_LIBS})

if(_runtime_rpaths)
    list(REMOVE_DUPLICATES _runtime_rpaths)
    set_target_properties(metal_metrology PROPERTIES BUILD_RPATH "${_runtime_rpaths}")
endif()

install(TARGETS metal_metrology RUNTIME DESTINATION bin)
