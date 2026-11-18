include(FetchContent)

function(LinkEIGEN TARGET ACCESS)
  FetchContent_Declare(
    eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4
  )

  FetchContent_GetProperties(eigen)
  if (NOT eigen_POPULATED)
    FetchContent_MakeAvailable(eigen)
    if (TARGET Eigen3)
      set_target_properties(Eigen3 PROPERTIES FOLDER ${PROJECT_NAME}/libs)
    endif()
    # target_include_directories(eigen PUBLIC
    #   ${eigen_SOURCE_DIR}
    # )
  endif()

  target_link_libraries(${TARGET} ${ACCESS} Eigen3::Eigen)
endfunction()
