include(FetchContent)

function(LinkCNPY TARGET ACCESS)
  FetchContent_Declare(
    cnpy
    GIT_REPOSITORY https://github.com/rogersce/cnpy.git
    GIT_TAG master
  )

  FetchContent_GetProperties(cnpy)

  if (NOT cnpy_POPULATED)
    FetchContent_MakeAvailable(cnpy)
    target_include_directories(cnpy PUBLIC
      ${cnpy_SOURCE_DIR}
    )
  endif()

  target_link_libraries(${TARGET} ${ACCESS} cnpy)
endfunction()
