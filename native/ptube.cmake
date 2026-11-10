# PTube's frontend adapts KOMI Tube; its shared media/HTTPS engine remains
# pinned with the documented scheduler correction. See native/UPSTREAM.md.
include("${CMAKE_CURRENT_LIST_DIR}/../upstream/komi-tube/native/native.cmake")
if(NOT PSP OR NOT PSP_BROWSER_LIBCURL_TRANSPORT OR PSP_BROWSER_CURL_STUB)
    return()
endif()
list(REMOVE_AT KOMI_RUNTIME_SOURCES 0)
list(PREPEND KOMI_RUNTIME_SOURCES "${CMAKE_CURRENT_LIST_DIR}/common/komi_runtime.c")
komi_program(ptube-app "PTube Native"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/app/main.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/discover.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/kanji.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/mylist.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/osk.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/search.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/ui.c"
        "${CMAKE_CURRENT_LIST_DIR}/app/yt_results.c"
    FILES
        "fonts/TilefinchSans-Regular.ttf=${CMAKE_CURRENT_SOURCE_DIR}/fonts/TilefinchSans-Regular.ttf"
        "fonts/LICENSE-TilefinchSans.txt=${CMAKE_CURRENT_SOURCE_DIR}/fonts/LICENSE-TilefinchSans.txt"
        "fonts/LICENSE-Unifont.txt=${CMAKE_CURRENT_SOURCE_DIR}/fonts/LICENSE-Unifont.txt")
