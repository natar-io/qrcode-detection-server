cmake_minimum_required (VERSION 3.2)
project (qrcode-server VERSION 0.1)

set (CMAKE_CXX_STANDARD 11)
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/module)

find_package(OpenCV REQUIRED core opencv_imgcodecs opencv_highgui opencv_imgproc opencv_videoio opencv_aruco)
if (NOT OpenCV_FOUND)
    message(FATAL_ERROR "Could not find OpenCV")
endif()

find_package(redisimagehelper REQUIRED)
find_package(hiredis REQUIRED)
find_package(RapidJSON REQUIRED)
find_package(ev REQUIRED)
find_package(cxxopts REQUIRED)
find_package(ZBar0 REQUIRED)

include_directories(${REDISIMAGEHELPER_INCLUDE_DIR})

set(QRCODE_DETECTION_SERVER
        src/server.cpp)
add_executable(qrcode-server ${QRCODE_DETECTION_SERVER})
target_link_libraries(qrcode-server ${OpenCV_LIBS} ${REDISIMAGEHELPER_LIBS} ${HIREDIS_LIBS} ${EV_LIBS} ${ZBAR_LIBRARIES})
