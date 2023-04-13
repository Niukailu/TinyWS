cmake_minimum_required(VERSION 3.10)

project(tinyws)

set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin/)

#添加调试信息
set(CMAKE_BUILD_TYPE Debug)

set(H1 ${PROJECT_SOURCE_DIR}/config)
set(H2 ${PROJECT_SOURCE_DIR}/http)
set(H3 ${PROJECT_SOURCE_DIR}/lock)
set(H4 ${PROJECT_SOURCE_DIR}/log)
set(H5 ${PROJECT_SOURCE_DIR}/mysql)
set(H6 ${PROJECT_SOURCE_DIR}/threadpool)
set(H7 ${PROJECT_SOURCE_DIR}/timer)
set(H8 ${PROJECT_SOURCE_DIR}/webserver)

include_directories(${H1} ${H2} ${H3} ${H4} ${H5} ${H6} ${H7} ${H8})

link_libraries(mysqlclient pthread)

aux_source_directory(${H1} src_files)
aux_source_directory(${H2} src_files)
aux_source_directory(${H3} src_files)
aux_source_directory(${H4} src_files)
aux_source_directory(${H5} src_files)
aux_source_directory(${H7} src_files)
aux_source_directory(${H8} src_files)

add_executable(${PROJECT_NAME} main.cpp ${src_files})