cmake_minimum_required(VERSION 3.14)
project(EchoAddonManagerSetup CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DUNICODE -D_UNICODE -DNOMINMAX)

# The installer's core: find Lossless Scaling, tell whose Lossless.dll is whose, install / update / repair / uninstall with backups and a rollback.
# It needs nothing but Windows.
add_library(setup_core STATIC
    src/core/fileinfo.cpp
    src/core/lsfolder.cpp
    src/core/state.cpp
    src/core/installer.cpp)
target_include_directories(setup_core PUBLIC src/core)

# Command line front end: detect, status, install, uninstall (also what the tests drive)
add_executable(setup_cli src/cli/main.cpp)
target_link_libraries(setup_cli PRIVATE setup_core)

# Stand-in DLLs for the tests: they carry a version resource and do nothing else
function(add_fake name product company version csv)
    set(FAKE_PRODUCT "${product}")
    set(FAKE_COMPANY "${company}")
    set(FAKE_VERSION "${version}")
    set(FAKE_VERSION_CSV "${csv}")
    configure_file(tests/fake.rc.in ${CMAKE_CURRENT_BINARY_DIR}/${name}.rc @ONLY)
    add_library(${name} SHARED EXCLUDE_FROM_ALL tests/fake.cpp ${CMAKE_CURRENT_BINARY_DIR}/${name}.rc)
    set_target_properties(${name} PROPERTIES PREFIX "" OUTPUT_NAME "${name}")
endfunction()
add_fake(fake_original     "Lossless Scaling"   "THS"                        "3.2.2.0" "3,2,2,0")
add_fake(fake_original_new "Lossless Scaling"   "THS"                        "3.2.3.0" "3,2,3,0")
add_fake(fake_ours_old     "Echo Addon Manager" "Echo Addon Manager project" "0.1.0"   "0,1,0,0")
add_fake(fake_other        "Something Else"     "Nobody"                     "1.0.0.0" "1,0,0,0")

# Offline test of the core against fake Lossless Scaling folders in %TEMP%: cmake --build . --config Release --target setup_test
add_executable(setup_test EXCLUDE_FROM_ALL tests/installer_test.cpp)
target_link_libraries(setup_test PRIVATE setup_core)
add_dependencies(setup_test fake_original fake_original_new fake_ours_old fake_other)
