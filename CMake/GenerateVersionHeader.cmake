set(build_id "")

if(GIT_EXECUTABLE)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${GIT_WORKING_DIR}
        OUTPUT_VARIABLE git_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_sha_result
        ERROR_QUIET
    )

    if(git_sha_result EQUAL 0 AND git_sha)
        set(build_id "${git_sha}")

        execute_process(
            COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
            WORKING_DIRECTORY ${GIT_WORKING_DIR}
            OUTPUT_VARIABLE git_dirty
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )

        if(git_dirty)
            set(build_id "${build_id}-dirty")
        endif()
    endif()
endif()

set(generated_content "#pragma once\n#define WON_BUILD_ID \"${build_id}\"\n")

set(temp_file "${OUTPUT_FILE}.tmp")
file(WRITE ${temp_file} "${generated_content}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${temp_file} ${OUTPUT_FILE})
file(REMOVE ${temp_file})
