# Version detection on a throwaway repository: exactly on a v tag, after it, and without Git.
#   cmake -DSOURCE=<repository> -DWORK=<folder> -P tests/version.cmake
include("${SOURCE}/cmake/detect_version.cmake")
find_package(Git REQUIRED)

file(REMOVE_RECURSE "${WORK}")
set(repo "${WORK}/repo")
file(MAKE_DIRECTORY "${repo}")
# The test folder is inside the build tree, which may itself be inside a checkout.
set(ENV{GIT_CEILING_DIRECTORIES} "${WORK}")
# A caller such as `git rebase -x` exports these; they would point every command below at
# the caller's repository.
foreach(var GIT_DIR GIT_WORK_TREE GIT_INDEX_FILE GIT_OBJECT_DIRECTORY GIT_COMMON_DIR
        GIT_ALTERNATE_OBJECT_DIRECTORIES GIT_PREFIX)
    unset(ENV{${var}})
endforeach()

function(git)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -c user.name=hdtrans -c user.email=hdtrans@localhost
        -c commit.gpgSign=false -c tag.gpgSign=false ${ARGN}
        WORKING_DIRECTORY "${repo}" RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "git ${ARGN} failed: ${error}")
    endif()
    set(output "${output}" PARENT_SCOPE)
endfunction()

function(expect version commit)
    hdtrans_detect_version("${repo}")
    if(NOT HDTRANS_VERSION STREQUAL version OR NOT HDTRANS_COMMIT STREQUAL commit)
        message(FATAL_ERROR "expected '${version}' '${commit}', got '${HDTRANS_VERSION}' '${HDTRANS_COMMIT}'")
    endif()
endfunction()

file(WRITE "${repo}/release-version.txt" "1.2.3\n")
git(init -q)
git(add release-version.txt)
git(commit -q --no-verify -m first)
git(rev-parse --short HEAD)
expect(dev "${output}")

git(tag v1.2.3)
git(tag other)
expect(1.2.3 "")

git(commit -q --no-verify --allow-empty -m second)
git(rev-parse --short HEAD)
expect(dev "${output}")

git(tag -a -m release v1.3.0)
expect(1.3.0 "")

file(REMOVE_RECURSE "${repo}/.git")
expect(1.2.3 "")
file(REMOVE "${repo}/release-version.txt")
expect(dev "")
