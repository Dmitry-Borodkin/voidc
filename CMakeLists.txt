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
    level-00/voidc_ast.cpp
    level-00/voidc_types.cpp
    level-00/voidc_target.cpp
    level-00/voidc_util.cpp
    level-00/voidc_main.cpp
    level-00/voidc_quark.cpp
    level-00/voidc_visitor.cpp
    level-00/voidc_compiler.cpp
    level-00/vpeg_parser.cpp
    level-00/vpeg_grammar.cpp
    level-00/vpeg_context.cpp
    level-00/vpeg_voidc.cpp
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



