application(NAME uringkv TYPE EXECUTABLE
            DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt ${XXHASH_TARGET} ${LIBURING_TARGET}
            INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include)
            
