# hdrulec lint on small programs over a one-file dictionary that names local rules 1 and 2:
# each case is the rules after L 0, the flag and how often lint reports it, joined by @.
#   cmake -DRULEC=<hdrulec> -DWORK=<dir> -P rules_lint_checks.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/dicts")
file(WRITE "${WORK}/dicts/base.tsv"
    "key\tko\tpos\tsem\tgram\tdict\trule\tid\n"
    "人気\t인기\tN110\t\t\tGN\t1\t1\n"
    "遥か\t아득%A1@\tG8U0\t\t\tGN\t2\t2\n")
set(cases
    # A number no row names is filled with an empty rule; a body under it is reported.
    "L 1 ||1 S+0== \"人気\"\nL 2 ||0\nL 3 ||0@unused-rule@0"
    "L 1 ||1 S+0== \"人気\"\nL 2 ||0\nL 3 ||1 S+1== \"の\"@unused-rule@1"
    # Two rules with one body are two rules.
    "L 1 ||1 P-1== \"1**\"\nL 2 ||1 P-1== \"1**\"\nL 3 ||0@duplicate-rule@0"
    "L 1 ||1 S-5== \"何\"\nL 2 ||0\nL 3 ||0@delta-range@0"
    "L 1 ||1 S-6== \"何\"\nL 2 ||0\nL 3 ||0@delta-range@1"
    # An ending may be empty; a word may not.
    "L 1 ||1 U-1== \"\"\nL 2 ||0\nL 3 ||0@empty-pattern@0"
    "L 1 ||1 S-1== \"\"\nL 2 ||0\nL 3 ||0@empty-pattern@1"
    # Four spaces stand for an empty conjugation marker.
    "L 1 ||0\nL 2 ||1 D+0= \"훨씬    \"\nL 3 ||0@literal-space@0"
    "L 1 ||0\nL 2 ||1 D+0= \"훨씬     \"\nL 3 ||0@literal-space@1"
    "L 1 ||0\nL 2 ||1 D+0= \"훨씬  \"\nL 3 ||0@literal-space@1"
    "L 1 ||0\nL 2 ||1 Y+0= \"다    \"\nL 3 ||0@literal-space@1")
set(i 0)
foreach(case IN LISTS cases)
    string(REPLACE "@" ";" parts "${case}")
    list(GET parts 0 program)
    list(GET parts 1 flag)
    list(GET parts 2 expected)
    set(fixture "${WORK}/case${i}.txt")
    file(WRITE "${fixture}" "rules 1 4 0\nL 0 ||0\n${program}\n")
    execute_process(COMMAND "${RULEC}" lint "${fixture}" -dicts "${WORK}/dicts" -tsv
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "case ${i}: lint failed: ${output}${error}")
    endif()
    string(REGEX MATCHALL "\t${flag}\t" found "${output}")
    list(LENGTH found count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR "case ${i}: expected ${expected} ${flag} warnings, got ${count}:\n${output}")
    endif()
    math(EXPR i "${i} + 1")
endforeach()
message(STATUS "${i} lint cases")

# -budget: a flag above its line fails, one at or below it passes; a flag without a line has 0.
file(WRITE "${WORK}/budget-case.txt" "rules 1 4 0\nL 0 ||0\nL 1 ||1 S-6== \"何\"\nL 2 ||0\nL 3 ||0\n")
set(budgets "delta-range\t1@0@-" "delta-range\t2@0@may be lowered to 1" "delta-range\t0@1@fix the new ones"
    "empty-pattern\t0@1@fix the new ones")
foreach(budget IN LISTS budgets)
    string(REPLACE "@" ";" parts "${budget}")
    list(GET parts 0 line)
    list(GET parts 1 expected)
    list(GET parts 2 note)
    file(WRITE "${WORK}/budget.tsv" "# test\n${line}\n")
    execute_process(COMMAND "${RULEC}" lint "${WORK}/budget-case.txt" -dicts "${WORK}/dicts"
        -budget "${WORK}/budget.tsv" RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT result EQUAL expected OR (NOT note STREQUAL "-" AND NOT error MATCHES "${note}"))
        message(FATAL_ERROR "budget ${line}: expected exit ${expected} and \"${note}\", got ${result}:\n${output}${error}")
    endif()
endforeach()
