find_package(GTest)

if (GTEST_FOUND)
  include(GoogleTest)
  add_subdirectory( XrdCl )
  if( ENABLE_HTTP )
    add_subdirectory(XrdHttpTests)
  endif()
endif()

add_subdirectory( common )
add_subdirectory( XrdClTests )
add_subdirectory( XrdSsiTests )

if( BUILD_XRDEC )
  add_subdirectory( XrdEcTests )
endif()

if( BUILD_CEPH )
  add_subdirectory( XrdCephTests )
endif()
