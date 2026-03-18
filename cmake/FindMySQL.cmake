# FindMySQL.cmake
# Find MySQL client library

# Try pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
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
    set(MYSQL_INCLUDE_DIRS "${MYSQL_INCLUDE_DIR}")
    set(MYSQL_LIBRARIES "${MYSQL_LIBRARY}")
    set(MYSQL_CLIENT_LIBRARIES "${MYSQL_LIBRARY}")
    set(MYSQL_INCLUDE_DIRS "${MYSQL_INCLUDE_DIR}")
  endif()
endif()

# Handle find_package arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySQL
  REQUIRED_VARS MYSQL_LIBRARIES MYSQL_INCLUDE_DIRS
  FOUND_VAR MYSQL_FOUND
)

# Mark as advanced
mark_as_advanced(MYSQL_INCLUDE_DIR MYSQL_LIBRARY)
