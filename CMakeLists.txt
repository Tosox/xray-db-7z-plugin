cmake_minimum_required(VERSION 3.21)
project(xdb_plugin LANGUAGES C CXX)

option(XDB_BUILD_TESTS "Build the unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT, so users don't need the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(Z7_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/7zip)
set(LZOKAY_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lzokay)
set(XRAY_RE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xray_re-tools)
set(XDB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/CPP/7zip/Archive/Xdb)

foreach(dep "${Z7_ROOT}/CPP/7zip/Archive/IArchive.h" "${LZOKAY_ROOT}/lzokay.hpp")
  if(NOT EXISTS "${dep}")
    message(FATAL_ERROR "Submodules are missing. Run:\n  git submodule update --init --recursive")
  endif()
endforeach()

# --- Plugin sources ---------------------------------------------------------

set(XDB_SOURCES
  ${XDB_DIR}/XdbCodec.cpp
  ${XDB_DIR}/XdbFormat.cpp
  ${XDB_DIR}/XdbHandler.cpp
  ${XDB_DIR}/XdbHandlerOut.cpp
  ${XDB_DIR}/XdbHeader.cpp
  ${XDB_DIR}/XdbIn.cpp
  ${XDB_DIR}/XdbLzhuf.cpp
  ${XDB_DIR}/XdbOut.cpp
  ${XDB_DIR}/XdbScrambler.cpp
  ${XDB_DIR}/XdbUpdate.cpp
)

# --- 7-Zip sources the handler needs ----------------------------------------

set(Z7_SOURCES
  ${Z7_ROOT}/CPP/7zip/Archive/Common/CoderMixer2.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/Common/HandlerOut.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/Common/InStreamWithCRC.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/Common/ItemNameUtils.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/Common/OutStreamWithCRC.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/Common/ParseProperties.cpp

  ${Z7_ROOT}/CPP/7zip/Common/CreateCoder.cpp
  ${Z7_ROOT}/CPP/7zip/Common/CWrappers.cpp
  ${Z7_ROOT}/CPP/7zip/Common/FilterCoder.cpp
  ${Z7_ROOT}/CPP/7zip/Common/InBuffer.cpp
  ${Z7_ROOT}/CPP/7zip/Common/InOutTempBuffer.cpp
  ${Z7_ROOT}/CPP/7zip/Common/LimitedStreams.cpp
  ${Z7_ROOT}/CPP/7zip/Common/MethodId.cpp
  ${Z7_ROOT}/CPP/7zip/Common/MethodProps.cpp
  ${Z7_ROOT}/CPP/7zip/Common/OutBuffer.cpp
  ${Z7_ROOT}/CPP/7zip/Common/ProgressUtils.cpp
  ${Z7_ROOT}/CPP/7zip/Common/PropId.cpp
  ${Z7_ROOT}/CPP/7zip/Common/StreamBinder.cpp
  ${Z7_ROOT}/CPP/7zip/Common/StreamObjects.cpp
  ${Z7_ROOT}/CPP/7zip/Common/StreamUtils.cpp
  ${Z7_ROOT}/CPP/7zip/Common/UniqBlocks.cpp
  ${Z7_ROOT}/CPP/7zip/Common/VirtThread.cpp

  ${Z7_ROOT}/CPP/7zip/Compress/CodecExports.cpp
  ${Z7_ROOT}/CPP/7zip/Compress/CopyCoder.cpp
  ${Z7_ROOT}/CPP/7zip/Compress/CopyRegister.cpp

  ${Z7_ROOT}/CPP/Common/CRC.cpp
  ${Z7_ROOT}/CPP/Common/CrcReg.cpp
  ${Z7_ROOT}/CPP/Common/IntToString.cpp
  ${Z7_ROOT}/CPP/Common/LzFindPrepare.cpp
  ${Z7_ROOT}/CPP/Common/MyString.cpp
  ${Z7_ROOT}/CPP/Common/MyVector.cpp
  ${Z7_ROOT}/CPP/Common/NewHandler.cpp
  ${Z7_ROOT}/CPP/Common/StdInStream.cpp
  ${Z7_ROOT}/CPP/Common/StdOutStream.cpp
  ${Z7_ROOT}/CPP/Common/StringConvert.cpp
  ${Z7_ROOT}/CPP/Common/StringToInt.cpp
  ${Z7_ROOT}/CPP/Common/UTFConvert.cpp
  ${Z7_ROOT}/CPP/Common/Wildcard.cpp

  ${Z7_ROOT}/CPP/Windows/FileDir.cpp
  ${Z7_ROOT}/CPP/Windows/FileFind.cpp
  ${Z7_ROOT}/CPP/Windows/FileIO.cpp
  ${Z7_ROOT}/CPP/Windows/FileName.cpp
  ${Z7_ROOT}/CPP/Windows/PropVariant.cpp
  ${Z7_ROOT}/CPP/Windows/Synchronization.cpp
  ${Z7_ROOT}/CPP/Windows/System.cpp
  ${Z7_ROOT}/CPP/Windows/TimeUtils.cpp

  ${Z7_ROOT}/C/7zCrc.c
  ${Z7_ROOT}/C/7zCrcOpt.c
  ${Z7_ROOT}/C/Alloc.c
  ${Z7_ROOT}/C/CpuArch.c
  ${Z7_ROOT}/C/LzFind.c
  ${Z7_ROOT}/C/Threads.c
)

# --- Core: everything except the DLL entry points ---------------------------
# An OBJECT library (not STATIC), so that self-registering objects such as the
# CRC table initializer in CRC.cpp are never dropped by the linker.

add_library(xdb_core OBJECT
  ${XDB_SOURCES}
  ${Z7_SOURCES}
  ${LZOKAY_ROOT}/lzokay.cpp
)

target_include_directories(xdb_core PUBLIC
  ${XDB_DIR}
  ${LZOKAY_ROOT}
  ${Z7_ROOT}/CPP
  ${Z7_ROOT}/CPP/7zip
  ${Z7_ROOT}/C
)

target_compile_definitions(xdb_core PUBLIC
  _WIN32_WINNT=0x0601
  UNICODE
  _UNICODE
  _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
  target_compile_options(xdb_core PUBLIC /utf-8 $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
  # Our own code is held to a stricter standard than the third-party sources.
  set_source_files_properties(${XDB_SOURCES} ${XDB_DIR}/XdbRegister.cpp PROPERTIES COMPILE_OPTIONS "/W4;/WX")
endif()

# --- Plugin DLL -------------------------------------------------------------

add_library(Xdb SHARED
  ${XDB_DIR}/XdbRegister.cpp
  ${XDB_DIR}/Xdb.def
  ${Z7_ROOT}/CPP/7zip/Archive/ArchiveExports.cpp
  ${Z7_ROOT}/CPP/7zip/Archive/DllExports2.cpp
)
target_link_libraries(Xdb PRIVATE xdb_core)

set_target_properties(Xdb PROPERTIES
  OUTPUT_NAME "XDB_$<IF:$<EQUAL:${CMAKE_SIZEOF_VOID_P},8>,x64,x86>"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Tests ------------------------------------------------------------------

if(XDB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
