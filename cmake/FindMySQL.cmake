# FindMySQL.cmake
# Find MySQL client library

# Prefer vcpkg's libmysql target when available
find_package(unofficial-libmysql CONFIG QUIET)
if(TARGET unofficial::libmysql::libmysql)
  set(MYSQL_FOUND TRUE)
  set(MYSQL_LIBRARIES unofficial::libmysql::libmysql)
  set(MYSQL_CLIENT_LIBRARIES unofficial::libmysql::libmysql)
  get_target_property(_mysql_target_includes unofficial::libmysql::libmysql
    INTERFACE_INCLUDE_DIRECTORIES)
  if(_mysql_target_includes)
    set(MYSQL_INCLUDE_DIRS "${_mysql_target_includes}")
  endif()
endif()

# Try pkg-config first
find_package(PkgConfig QUIET)
if(NOT MYSQL_FOUND AND PKG_CONFIG_FOUND)
  pkg_check_modules(MYSQL QUIET mysqlclient)
endif()

# If pkg-config failed, try manual search
if(NOT MYSQL_FOUND)
  # Common installation paths
  set(MYSQL_HINTS
    /usr/local
    /usr
    /opt/mysql
    /opt/homebrew
    /opt/homebrew/opt/mysql-client
    /usr/local/opt/mysql-client
    "$ENV{ProgramFiles}/MySQL"
    "$ENV{MYSQL_ROOT}"
  )

  find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    HINTS ${MYSQL_HINTS}
    PATH_SUFFIXES mysql include include/mysql
  )

  # Find library with different possible names
  find_library(MYSQL_LIBRARY
    NAMES
      mysqlclient
      libmysqlclient
      mysqlclient_r
    HINTS ${MYSQL_HINTS}
    PATH_SUFFIXES lib lib64 lib/mysql
  )

  if(MYSQL_INCLUDE_DIR AND MYSQL_LIBRARY)
    set(MYSQL_FOUND TRUE)
    set(MYSQL_LIBRARIES "${MYSQL_LIBRARY}")
    set(MYSQL_CLIENT_LIBRARIES "${MYSQL_LIBRARY}")
    set(MYSQL_INCLUDE_DIRS "${MYSQL_INCLUDE_DIR}")
  endif()
endif()

if(MYSQL_FOUND AND NOT MYSQL_INCLUDE_DIRS)
  find_path(MYSQL_INCLUDE_DIR
    NAMES mysql/mysql.h mysql.h
    HINTS ${MYSQL_HINTS}
    PATH_SUFFIXES include include/mysql mysql
  )
  if(MYSQL_INCLUDE_DIR)
    set(MYSQL_INCLUDE_DIRS "${MYSQL_INCLUDE_DIR}")
  endif()
endif()

# Normalize include directories so code using <mysql/mysql.h> works whether the
# discovery step found .../include or .../include/mysql.
if(MYSQL_INCLUDE_DIRS)
  set(_normalized_mysql_include_dirs "")
  foreach(_dir IN LISTS MYSQL_INCLUDE_DIRS)
    if(EXISTS "${_dir}/mysql.h" AND IS_DIRECTORY "${_dir}")
      get_filename_component(_dir_name "${_dir}" NAME)
      if(_dir_name STREQUAL "mysql")
        get_filename_component(_parent_dir "${_dir}" DIRECTORY)
        list(APPEND _normalized_mysql_include_dirs "${_parent_dir}")
      else()
        list(APPEND _normalized_mysql_include_dirs "${_dir}")
      endif()
    else()
      list(APPEND _normalized_mysql_include_dirs "${_dir}")
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _normalized_mysql_include_dirs)
  set(MYSQL_INCLUDE_DIRS "${_normalized_mysql_include_dirs}")
endif()

# Handle find_package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQL
  REQUIRED_VARS MYSQL_LIBRARIES MYSQL_INCLUDE_DIRS
  FOUND_VAR MYSQL_FOUND
)

# Mark as advanced
mark_as_advanced(MYSQL_INCLUDE_DIR MYSQL_LIBRARY)
