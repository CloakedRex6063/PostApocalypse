cmake_minimum_required(VERSION 4.1)
project(PostApocalypticGame)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/CPM.cmake)

function(copy_dirs TARGET_NAME)
    foreach(DIR ${ARGN})
        cmake_path(GET DIR STEM DIR_NAME)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${DIR}"
                "${CMAKE_BINARY_DIR}/game/${DIR_NAME}"
                COMMENT "Copying ${DIR}"
        )
    endforeach()
endfunction()

add_subdirectory(engine)
add_subdirectory(game)


find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_custom_target(run_python_script
        COMMAND ${CMAKE_COMMAND} -E echo "Generating Shaders..."
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/gen_shaders.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Generating Shaders"
        VERBATIM
)
add_dependencies(Engine run_python_script)