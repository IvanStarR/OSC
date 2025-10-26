application(NAME gitconfig TYPE EXECUTABLE
    DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt ${APP_NAME}.server
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include)

