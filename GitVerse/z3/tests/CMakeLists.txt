test(NAME smoke
    DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt 
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include)
