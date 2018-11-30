# Attempt to find the desired version, but fall back to other
# additional versions.
if (NOT PYTHONINTERP_FOUND)
find_package (PythonInterp ${PYTHON_VERSION} REQUIRED)
endif ()

# The version that was found may not be the default or user
# defined one.
set (PYTHON_VERSION_FOUND ${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR})

if (NOT ${PYTHON_VERSION} EQUAL ${PYTHON_VERSION_FOUND} )
    message (WARNING "The requested version ${PYTHON_VERSION} was not found.") 
    message (WARNING "Using ${PYTHON_VERSION_FOUND} instead.")
endif ()

find_package (PythonLibs ${PYTHON_VERSION_FOUND} REQUIRED)

if (NOT DEFINED PYTHON_SITE_DIR)
    set (PYTHON_SITE_DIR "${CMAKE_INSTALL_LIBDIR}/python${PYTHON_VERSION_FOUND}/site-packages")
endif ()


set (target_name PyOSL)
set (python_srcs py_osl.cpp)

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    # Seems to be a problem on some systems, with pybind11 and python headers
    set_property (SOURCE ${python_srcs} APPEND_STRING PROPERTY COMPILE_FLAGS " -Wno-macro-redefined ")
endif ()

message (STATUS "Python version ${PYTHON_VERSION_STRING}")
if (VERBOSE)
    message (STATUS "Python found ${PYTHONLIBS_FOUND} ")
    message (STATUS "Python include dirs ${PYTHON_INCLUDE_PATH}")
    message (STATUS "Python libraries    ${PYTHON_LIBRARIES}")
    message (STATUS "Python site packages dir ${PYTHON_SITE_DIR}")
    message (STATUS "Python to include 'lib' prefix: ${PYLIB_LIB_PREFIX}")
    message (STATUS "Python to include SO version: ${PYLIB_INCLUDE_SONAME}")
    message (STATUS "Python version major: ${PYTHON_VERSION_MAJOR} minor: ${PYTHON_VERSION_MINOR}")
    message (STATUS "pybind11 headers ${PYBIND11_INCLUDE_DIR}")
endif ()

include_directories (${PYTHON_INCLUDE_PATH} ${PYBIND11_INCLUDE_DIR})

add_library (${target_name} MODULE ${python_srcs})
target_compile_options(${target_name} PUBLIC -frtti)
target_link_libraries (${target_name} oslquery)


if(NOT PYLIB_LIB_PREFIX)
    target_compile_definitions(${target_name} PRIVATE "-DOSL_PYMODULE_NAME=OSL${PYMODULE_SUFFIX}")
    set_target_properties (${target_name} PROPERTIES
                             OUTPUT_NAME OSL
                             PREFIX "")
else()
    target_compile_definitions(${target_name} PRIVATE "-DOSL_PYMODULE_NAME=PyOSL${PYMODULE_SUFFIX}")
    set_target_properties (${target_name} PROPERTIES
                             OUTPUT_NAME PyOSL
                             PREFIX lib)
endif ()

if(PYLIB_INCLUDE_SONAME)
    if (VERBOSE)
        message(STATUS "Setting PyOSL SOVERSION to: ${SOVERSION}")
    endif ()
    set_target_properties(${target_name} PROPERTIES
        VERSION ${OSL_VERSION_MAJOR}.${OSL_VERSION_MINOR}
        SOVERSION ${SOVERSION}
    )
endif()

if (WIN32)
    set_target_properties (${target_name} PROPERTIES
                           DEBUG_POSTFIX "_d"
                           SUFFIX ".pyd")
endif()

install (TARGETS ${target_name}
         RUNTIME DESTINATION ${PYTHON_SITE_DIR} COMPONENT user
         LIBRARY DESTINATION ${PYTHON_SITE_DIR} COMPONENT user)
