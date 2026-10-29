cmake_minimum_required(VERSION 3.21)
project(Smail VERSION 0.1.6 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Sql)

if(WIN32)
    enable_language(RC)
    configure_file(resources/smail.rc.in ${CMAKE_CURRENT_BINARY_DIR}/smail.rc @ONLY)
    configure_file(assets/smail.ico ${CMAKE_CURRENT_BINARY_DIR}/smail.ico COPYONLY)
    configure_file(assets/smail_under.ico ${CMAKE_CURRENT_BINARY_DIR}/smail_under.ico COPYONLY)
endif()

add_library(smail_core
    src/core/Config.cpp
    src/core/Mime.cpp
    src/core/Store.cpp
    src/core/CredentialStore.cpp
    src/core/ReplyPolicy.cpp
    src/core/ReplyMessage.cpp
    src/net/MailClient.cpp
    src/net/TlsConnection.cpp
)
target_include_directories(smail_core PUBLIC include)
target_link_libraries(smail_core PUBLIC Qt6::Core Qt6::Network Qt6::Sql Qt6::Gui)
if(WIN32)
    target_link_libraries(smail_core PRIVATE crypt32)
endif()

add_executable(Smail WIN32
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/Collector.cpp
    src/app/ReplyBodyEditor.cpp
    resources/icons.qrc
    include/smail/MainWindow.h
    include/smail/Collector.h
)
if(WIN32)
    target_sources(Smail PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/smail.rc)
endif()
target_link_libraries(Smail PRIVATE smail_core Qt6::Widgets)

if(MSVC)
    target_compile_options(smail_core PUBLIC /utf-8 /W4)
else()
    target_compile_options(smail_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS Smail RUNTIME DESTINATION bin)
