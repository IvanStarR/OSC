test(NAME smoke
    DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt ${LIBURING_TARGET}
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include)
