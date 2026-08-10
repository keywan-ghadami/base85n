cmake_minimum_required(VERSION 3.10)
project(base85n C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(base85n STATIC src/base85n.c)
target_include_directories(base85n PUBLIC include)
target_compile_options(base85n PRIVATE -Wall -Wextra -Werror)

enable_testing()
add_executable(test_base85n tests/test_base85n.c)
target_link_libraries(test_base85n PRIVATE base85n)
target_compile_options(test_base85n PRIVATE -Wall -Wextra -Werror)
# The shared test vectors live outside this directory; tell the test where
# they are so it does not depend on the working directory ctest starts it in.
target_compile_definitions(test_base85n PRIVATE
    BASE85N_TESTVECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../testvectors")
add_test(NAME test_base85n COMMAND test_base85n)
