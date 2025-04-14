cmake_minimum_required(VERSION 3.30)
project(smtx C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

function(detect_cache_line_size RESULT_VAR)
    set(DEFAULT_CACHE_LINE_SIZE 64)

    if(UNIX AND NOT APPLE)
        execute_process(
                COMMAND getconf LEVEL1_DCACHE_LINESIZE
                OUTPUT_VARIABLE DETECTED_CACHE_LINE_SIZE
                RESULT_VARIABLE GETCONF_RESULT
                OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(GETCONF_RESULT EQUAL 0 AND DETECTED_CACHE_LINE_SIZE)
            if(DETECTED_CACHE_LINE_SIZE GREATER 0)
                set(${RESULT_VAR} ${DETECTED_CACHE_LINE_SIZE} PARENT_SCOPE)
                return()
            endif()
        endif()
    endif()

    set(${RESULT_VAR} ${DEFAULT_CACHE_LINE_SIZE} PARENT_SCOPE)
endfunction()

detect_cache_line_size(CACHE_LINE_SIZE)
add_compile_definitions(CACHE_LINE_SIZE=${CACHE_LINE_SIZE})

add_executable(smtx examples/smtx-example.c examples/smtx.c)
