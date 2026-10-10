cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(ddlc_engine LANGUAGES C CXX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

set(NZATO_XVID_SOURCES
  third_party/nvp/mpeg4_xvid.c
  third_party/nvp/xvid/xvid.c third_party/nvp/xvid/decoder.c
  third_party/nvp/xvid/bitstream/bitstream.c third_party/nvp/xvid/bitstream/cbp.c
  third_party/nvp/xvid/bitstream/mbcoding.c
  third_party/nvp/xvid/dct/fdct.c third_party/nvp/xvid/dct/idct.c
  third_party/nvp/xvid/dct/simple_idct.c
  third_party/nvp/xvid/image/arm/yv12_to_rgb565.c
  third_party/nvp/xvid/image/colorspace.c third_party/nvp/xvid/image/font.c
  third_party/nvp/xvid/image/image.c third_party/nvp/xvid/image/interpolate8x8.c
  third_party/nvp/xvid/image/postprocessing.c third_party/nvp/xvid/image/qpel.c
  third_party/nvp/xvid/image/reduced.c
  third_party/nvp/xvid/motion/estimation_common.c third_party/nvp/xvid/motion/gmc.c
  third_party/nvp/xvid/motion/motion_comp.c third_party/nvp/xvid/motion/sad.c
  third_party/nvp/xvid/prediction/mbprediction.c
  third_party/nvp/xvid/quant/quant_h263.c third_party/nvp/xvid/quant/quant_matrix.c
  third_party/nvp/xvid/quant/quant_mpeg.c
  third_party/nvp/xvid/utils/emms.c third_party/nvp/xvid/utils/mbtransquant.c
  third_party/nvp/xvid/utils/mem_align.c third_party/nvp/xvid/utils/mem_transfer.c
  third_party/nvp/xvid/utils/sram_tables.c third_party/nvp/xvid/utils/timer.c)

add_library(nzato_core
  src/pack.cpp src/navigation.cpp src/info_pages.cpp src/player.cpp src/raster.cpp
  src/session.cpp src/ui.cpp src/ui_common.cpp src/nvp.cpp src/video.cpp
  ${NZATO_XVID_SOURCES})
target_include_directories(nzato_core PUBLIC include PRIVATE third_party/nvp third_party/nvp/xvid)
target_compile_definitions(nzato_core PRIVATE XVID_DECODER_ONLY)
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  target_compile_definitions(nzato_core PRIVATE ARCH_IS_64BIT ARCH_IS_GENERIC)
else()
  target_compile_definitions(nzato_core PRIVATE ARCH_IS_32BIT ARCH_IS_GENERIC)
endif()

if(MSVC)
  # /Ob2 miscompiles the bundled decoder with current MSVC 19.44.
  target_compile_options(nzato_core PRIVATE /W4 /permissive- /EHsc- /GR- /utf-8 /Ob1)
else()
  target_compile_options(nzato_core PRIVATE -Wall -Wextra -Wpedantic -fno-exceptions -fno-rtti)
endif()

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format clang-format.exe)
if(CLANG_FORMAT_EXECUTABLE)
  file(GLOB_RECURSE NZATO_FORMAT_SOURCES CONFIGURE_DEPENDS
    src/*.cpp include/*.hpp platform/*.cpp)
  add_custom_target(format
    COMMAND ${CLANG_FORMAT_EXECUTABLE} -i ${NZATO_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
  add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_EXECUTABLE} --dry-run --Werror ${NZATO_FORMAT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(WIN32)
  add_executable(ddlc WIN32 platform/win32/main.cpp)
  target_compile_definitions(ddlc PRIVATE NZATO_LAUNCHER_CHINESE=0 NZATO_LAUNCHER_DEBUG=0)
  target_link_libraries(ddlc PRIVATE nzato_core shell32)
  if(EXISTS "${CMAKE_SOURCE_DIR}/dist/ndless/ddlc.nzp.tns")
    add_custom_command(TARGET ddlc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/dist/ndless/ddlc.nzp.tns"
        $<TARGET_FILE_DIR:ddlc>/ddlc.nzp)
  endif()
endif()
