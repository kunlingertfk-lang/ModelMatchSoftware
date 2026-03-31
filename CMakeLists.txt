cmake_minimum_required(VERSION 3.10)

project(ModelShapeMatch LANGUAGES CXX)


#检查是否使用MSVC编辑器
if(MSVC)
    #添加 /utf-8 编译选项
    add_compile_options(/utf-8)
endif()


set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

#OpenCV
set(OpenCV_DIR "D:/3rdparty/opencv/opencv-4.8.0/install_debug_msvc")
#set(OpenCV_DIR "D:/LIB/OpenCV/opencv_4.8.0_a/opencvb_build_release_x64/install")
set(HALCON_ROOT "D:/LIB/Halcon")

find_package(OpenCV REQUIRED
    core
    imgproc
    highgui
    imgcodecs
)
message(STATUS "OpenCV version: ${OpenCV_VERSION}")
message(STATUS "OpenCV include: ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV libs: ${OpenCV_LIBS}")


#Qt
find_package(Qt5 REQUIRED COMPONENTS Widgets)

#Halcon

add_executable(ModelShapeMatch
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    AbstractRoiItem.h AbstractRoiItem.cpp
    RotrectRoiItem.h RotrectRoiItem.cpp
    roitoolbar.h roitoolbar.cpp
    roigraphicsview.h roigraphicsview.cpp
    images.qrc
    matchresultitem.h matchresultitem.cpp
    halconworker.h halconworker.cpp
    MatchResult.h
    resource/qss.qrc
    circleroiitem.h circleroiitem.cpp
    rect1roiitem.h rect1roiitem.cpp
)

include_directories(
    ${HALCON_ROOT}/include
    ${HALCON_ROOT}/include/halconcpp
)

target_link_libraries(ModelShapeMatch PRIVATE
    ${OpenCV_LIBS}
    Qt5::Widgets
    ${HALCON_ROOT}/lib/x64-win64/halcon.lib
    ${HALCON_ROOT}/lib/x64-win64/halconcpp.lib
)
