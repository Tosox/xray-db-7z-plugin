include(FetchContent)

FetchContent_Declare(googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  URL_HASH SHA256=f179ec217f9b3b3f3c6e8b02d3e7eda997b49e4ce26d6b235c9053bec9c0bf9f
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(xdb_tests
  HeaderTest.cpp
  InArchiveTest.cpp
  InitGuid.cpp
  LzhufTest.cpp
  LzoTest.cpp
  RealArchivesTest.cpp
  ScramblerTest.cpp
  SmokeTest.cpp
  ${Z7_ROOT}/CPP/7zip/Common/FileStreams.cpp
)

target_link_libraries(xdb_tests PRIVATE xdb_core GTest::gtest_main)

# The smoke test loads the real plugin DLL.
add_dependencies(xdb_tests Xdb)
target_compile_definitions(xdb_tests PRIVATE XDB_PLUGIN_PATH="$<TARGET_FILE:Xdb>")

include(GoogleTest)
gtest_discover_tests(xdb_tests DISCOVERY_MODE PRE_TEST)
