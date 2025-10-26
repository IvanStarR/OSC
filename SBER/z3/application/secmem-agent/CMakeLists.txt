application(NAME secmem-agent TYPE EXECUTABLE
    DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt OpenSSL::Crypto
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include
    ${asio_SOURCE_DIR}/asio/include)

