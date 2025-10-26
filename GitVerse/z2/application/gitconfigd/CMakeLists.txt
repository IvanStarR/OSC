set(D_NAME server)
module(NAME ${D_NAME}
    TYPE EXECUTABLE
    DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt
    INCLUDE_DIRECTORIES
        ${CMAKE_SOURCE_DIR}/include
        ${asio_SOURCE_DIR}/asio/include
)