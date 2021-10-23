set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

cmake_minimum_required(VERSION 3.13.4)

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "No build type selected, default to Debug")
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (default Debug)" FORCE)
endif()

string(TOUPPER "${CMAKE_BUILD_TYPE}" uppercase_CMAKE_BUILD_TYPE)


#----------------------------------------------------------------------
project(Voidc)
#----------------------------------------------------------------------


find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)


include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})


#----------------------------------------------------------------------
add_executable(voidc
    voidc_ast.cpp
    voidc_types.cpp
    voidc_target.cpp
    voidc_util.cpp
    voidc_main.cpp
    voidc_quark.cpp
    voidc_visitor.cpp
    voidc_compiler.cpp
    vpeg_parser.cpp
    vpeg_grammar.cpp
    vpeg_context.cpp
    vpeg_voidc.cpp
)


#----------------------------------------------------------------------
if (UNIX)

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL DEBUG)

       set(MYFLAGS -g -fsanitize=address -fno-omit-frame-pointer)

    endif()

    if (${uppercase_CMAKE_BUILD_TYPE} STREQUAL RELEASE)

       set(MYFLAGS -O3 -Oz -DNDEBUG)

    endif()

    target_compile_options(voidc PRIVATE ${MYFLAGS})

    target_link_options(voidc PRIVATE -rdynamic ${MYFLAGS})

endif (UNIX)


if (MINGW)

    target_compile_options(voidc PRIVATE -O3 -fcxx-exceptions -IC:/Tools/include -frtti)

endif (MINGW)


# Link against LLVM library(!)
target_link_libraries(voidc LLVM)



