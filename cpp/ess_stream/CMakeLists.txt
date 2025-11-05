cmake_minimum_required(VERSION 3.16)

project(ess_stream VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Prefer Release unless specified
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Use pkg-config to locate Essentia and its dependencies (FFTW, Eigen, etc.)
find_package(PkgConfig REQUIRED)

# Set PKG_CONFIG_PATH to include our local Essentia installation
set(ENV{PKG_CONFIG_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/essentia/_install/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")

# Find Essentia using pkg-config
pkg_check_modules(ESSENTIA REQUIRED IMPORTED_TARGET essentia)

# Find PortAudio
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED IMPORTED_TARGET portaudio-2.0)

# Find ZeroMQ
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED IMPORTED_TARGET libzmq)

# Add cppzmq include directory
include_directories(/opt/homebrew/include)

# Manually set paths as backup
set(ESSENTIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/essentia/_install")
set(ESSENTIA_INCLUDE_DIRS "${ESSENTIA_ROOT}/include")
set(ESSENTIA_LIBRARIES "${ESSENTIA_ROOT}/lib/libessentia.dylib")

add_executable(ess_stream
  src/app/streaming_pipe.cpp
  src/app/sumrange.cpp
  src/app/hit_gate_quantile.cpp
  src/app/hit_gate_multiframe.cpp
  src/app/hit_gate_onset.cpp
  src/app/vector_index.cpp
  src/app/vector_pack5.cpp
  src/app/instrument_sum.cpp
  src/app/instrument_predictor.cpp
  src/app/zeromq_publisher.cpp
  src/app/hit_prediction_logger.cpp
  src/app/gate_logger_sink.cpp
)

target_include_directories(ess_stream PRIVATE
  PkgConfig::ESSENTIA
  PkgConfig::PORTAUDIO
  PkgConfig::ZMQ
  ${ESSENTIA_INCLUDE_DIRS}
  ${ESSENTIA_ROOT}/include
)

target_link_libraries(ess_stream PRIVATE
  PkgConfig::ESSENTIA
  PkgConfig::PORTAUDIO
  PkgConfig::ZMQ
  ${ESSENTIA_LIBRARIES}
)

target_compile_definitions(ess_stream PRIVATE
  $<$<CONFIG:Debug>:ESS_STREAM_DEBUG>
)

message(STATUS "Using Essentia from: ${ESSENTIA_PREFIX}")


