cmake_minimum_required(VERSION 3.16)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_CURRENT_SOURCE_DIR}/include/ki_xml_pdf.h")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/include/ki_xml_pdf.h" KI_VERSION_LINE
     REGEX "^#define KI_XML_PDF_VERSION \"")
string(REGEX REPLACE ".*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1" KI_VERSION "${KI_VERSION_LINE}")
project(ki_xml_pdf VERSION "${KI_VERSION}" LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ki_xml_pdf is validated on 64-bit Linux with glibc")
endif()
include(CheckCSourceCompiles)
check_c_source_compiles("#include <features.h>\n#ifndef __GLIBC__\n#error glibc required\n#endif\nint main(void) { return 0; }" KI_HAVE_GLIBC)
if(NOT KI_HAVE_GLIBC)
    message(FATAL_ERROR "ki_xml_pdf requires glibc (fopencookie)")
endif()

include(GNUInstallDirs)
include(CTest)
find_package(ZLIB REQUIRED)
option(BUILD_SHARED_LIBS "Select the shared library for the default package target" ON)
set(KI_CORE_SOURCES src/xml_parse.c src/font.c src/geometry.c src/info.c src/pdf_write.c
                    src/layout.c src/report.c src/convert.c)
set(KI_FONT_PATH "${CMAKE_INSTALL_FULL_DATADIR}/ki_xml_pdf/DejaVuSans.ttf")

function(ki_core_settings target)
    set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES
                          POSITION_INDEPENDENT_CODE YES)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L
                               "XML2PDF_FONT_PATH=\"${KI_FONT_PATH}\"")
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
        PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
               "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
endfunction()

add_library(ki_xml_pdf_static STATIC ${KI_CORE_SOURCES})
add_library(ki_xml_pdf_shared SHARED ${KI_CORE_SOURCES})
ki_core_settings(ki_xml_pdf_static)
ki_core_settings(ki_xml_pdf_shared)
# Exported static targets carry $<LINK_ONLY:ZLIB::ZLIB>; the shared library
# records libz as a NEEDED entry.
target_link_libraries(ki_xml_pdf_static PRIVATE ZLIB::ZLIB)
target_link_libraries(ki_xml_pdf_shared PRIVATE ZLIB::ZLIB)
set_target_properties(ki_xml_pdf_static PROPERTIES OUTPUT_NAME ki_xml_pdf EXPORT_NAME static)
set_target_properties(ki_xml_pdf_shared PROPERTIES OUTPUT_NAME ki_xml_pdf EXPORT_NAME shared
                      VERSION "${PROJECT_VERSION}" SOVERSION 0
                      LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libki_xml_pdf.map")
target_link_options(ki_xml_pdf_shared PRIVATE
                    "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libki_xml_pdf.map")
add_library(ki_xml_pdf::static ALIAS ki_xml_pdf_static)
add_library(ki_xml_pdf::shared ALIAS ki_xml_pdf_shared)
if(BUILD_SHARED_LIBS)
    add_library(ki_xml_pdf::ki_xml_pdf ALIAS ki_xml_pdf_shared)
else()
    add_library(ki_xml_pdf::ki_xml_pdf ALIAS ki_xml_pdf_static)
endif()

add_executable(xml2pdf src/main.c)
set_target_properties(xml2pdf PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
file(RELATIVE_PATH KI_CLI_LIB_REL "${CMAKE_INSTALL_FULL_BINDIR}" "${CMAKE_INSTALL_FULL_LIBDIR}")
set_target_properties(xml2pdf PROPERTIES INSTALL_RPATH "$ORIGIN/${KI_CLI_LIB_REL}")
target_compile_definitions(xml2pdf PRIVATE _POSIX_C_SOURCE=200809L)
target_link_libraries(xml2pdf PRIVATE ki_xml_pdf::ki_xml_pdf)
# The build-tree executable can find its font from any working directory.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/fonts")
configure_file(fonts/DejaVuSans.ttf fonts/DejaVuSans.ttf COPYONLY)

add_executable(catalog_example examples/catalog_driver.c examples/catalog_map.c)
set_target_properties(catalog_example PROPERTIES OUTPUT_NAME catalog C_STANDARD 99
                      C_STANDARD_REQUIRED YES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/examples")
target_compile_options(catalog_example PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(catalog_example PRIVATE ki_xml_pdf::ki_xml_pdf)

if(BUILD_TESTING)
    find_package(Threads REQUIRED)
    # Internal parser tests need symbols deliberately hidden from the shared ABI.
    add_executable(test_stream tests/test_stream.c ${KI_CORE_SOURCES})
    ki_core_settings(test_stream)
    target_link_libraries(test_stream PRIVATE Threads::Threads ZLIB::ZLIB)
    target_link_options(test_stream PRIVATE "LINKER:--wrap=fsync" "LINKER:--wrap=deflate"
                        "LINKER:--wrap=deflateInit2_")
    add_test(NAME stream COMMAND test_stream)
    set_tests_properties(stream PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME catalog COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/catalog_verify.py"
        --binary "$<TARGET_FILE:catalog_example>"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/examples/catalog.xml"
        --oracle "${CMAKE_CURRENT_SOURCE_DIR}/examples/catalog.expected.txt")
    add_test(NAME cli_pipeline COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_pipeline.py"
        --binary "$<TARGET_FILE:xml2pdf>"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/examples/report-v1.xml"
        --oracle "${CMAKE_CURRENT_SOURCE_DIR}/examples/report-v1.expected.txt")
    add_test(NAME pdf_offsets COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pdf_offsets.py"
        --binary "$<TARGET_FILE:xml2pdf>"
        --fixture "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixture.xml")
    set_tests_properties(pdf_offsets PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_test(NAME pdf_streams COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/pdf_streams.py"
        --binary "$<TARGET_FILE:xml2pdf>"
        --font "${CMAKE_CURRENT_SOURCE_DIR}/fonts/DejaVuSans.ttf")
    add_test(NAME wrap_check COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/wrap_check.py"
        --binary "$<TARGET_FILE:xml2pdf>")
    add_test(NAME geometry_check COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/geometry_check.py"
        --binary "$<TARGET_FILE:xml2pdf>")
    add_test(NAME info_check COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/info_check.py"
        --binary "$<TARGET_FILE:xml2pdf>")
    add_test(NAME integration_check COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration_check.py"
        --binary "$<TARGET_FILE:xml2pdf>")
endif()

set(KI_DEFAULT_SHARED "${BUILD_SHARED_LIBS}")
set(KI_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(KI_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(KI_PREFIX "${CMAKE_INSTALL_PREFIX}")
set(KI_LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")
set(KI_INCLUDEDIR "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
configure_file(cmake/ki_xml_pdfConfig.cmake.in ki_xml_pdfConfig.cmake @ONLY)
configure_file(cmake/ki_xml_pdfConfigVersion.cmake.in ki_xml_pdfConfigVersion.cmake @ONLY)
configure_file(cmake/ki_xml_pdf.pc.in ki_xml_pdf.pc @ONLY)
install(TARGETS ki_xml_pdf_static ki_xml_pdf_shared xml2pdf EXPORT ki_xml_pdfTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(EXPORT ki_xml_pdfTargets NAMESPACE ki_xml_pdf::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki_xml_pdf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ki_xml_pdfConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/ki_xml_pdfConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ki_xml_pdf")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ki_xml_pdf.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(FILES include/ki_xml_pdf.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES fonts/DejaVuSans.ttf fonts/DejaVuSans.copyright LICENSE
        DESTINATION "${CMAKE_INSTALL_DATADIR}/ki_xml_pdf")
