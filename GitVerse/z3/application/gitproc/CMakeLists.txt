application(NAME gitproc TYPE EXECUTABLE
            DEPENDENCIES ${APP_NAME}.core spdlog::spdlog fmt::fmt
            INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/include)
            
