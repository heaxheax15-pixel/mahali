add_executable(mahali-desktop main.cpp)

target_link_libraries(mahali-desktop
    PRIVATE
        mahali-ui
        Qt${QT_VERSION_MAJOR}::Widgets)

target_compile_definitions(mahali-desktop
    PRIVATE
        MAHALI_VERSION="${PROJECT_VERSION}")

# Windows-only packaging bits: the taskbar/window icon and the exe metadata come
# from mahali.rc, which the RC compiler embeds into the binary. mahali-orange.ico
# is resolved relative to mahali.rc, so both files must stay side by side.
# WIN32_EXECUTABLE links the GUI subsystem so no console window appears; that
# changes the entry point to wWinMain, which only exists in Qt's WinMain
# library, hence the extra MSVC link.
if(WIN32)
    set_target_properties(mahali-desktop PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "mahali-desktop")
    target_sources(mahali-desktop PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/mahali.rc")
    if(MSVC)
        target_link_libraries(mahali-desktop PRIVATE Qt${QT_VERSION_MAJOR}::WinMain)
    endif()
endif()

# Classic lrelease pipeline (the Qt6LinguistTools package that provides
# qt_add_translations is not always installed). Each .ts is compiled to a .qm
# and the results are embedded under the /i18n resource prefix, so the
# catalogues ship inside the binary and load from ":/i18n/mahali_<code>.qm".
set(TS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/translations)
file(MAKE_DIRECTORY ${TS_BUILD_DIR})

set(TS_QM_FILES)
set(TS_QRC_BODY "")
foreach(ts_file IN LISTS TS_FILES)
    get_filename_component(ts_stem ${ts_file} NAME_WE)
    set(ts_abs "${PROJECT_SOURCE_DIR}/${ts_file}")
    set(qm_file "${TS_BUILD_DIR}/${ts_stem}.qm")
    add_custom_command(
        OUTPUT ${qm_file}
        COMMAND ${MAHALI_LRELEASE} ${ts_abs} -qm ${qm_file}
        DEPENDS ${ts_abs}
        COMMENT "lrelease ${ts_file}"
        VERBATIM)
    list(APPEND TS_QM_FILES ${qm_file})
    string(APPEND TS_QRC_BODY "        <file>${ts_stem}.qm</file>\n")
endforeach()

file(WRITE ${TS_BUILD_DIR}/mahali_translations.qrc
"<!DOCTYPE RCC>
<RCC>
    <qresource prefix=\"/i18n\">
${TS_QRC_BODY}    </qresource>
</RCC>
")

add_custom_target(mahali_translations ALL DEPENDS ${TS_QM_FILES})
add_dependencies(mahali-desktop mahali_translations)
target_sources(mahali-desktop PRIVATE ${TS_QM_FILES} ${TS_BUILD_DIR}/mahali_translations.qrc)
