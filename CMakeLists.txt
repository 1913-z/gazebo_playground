cmake_minimum_required(VERSION 3.10)
project(gazebo_sim)
find_package(OpenMP REQUIRED)
set(CMAKE_CXX_STANDARD        17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_TYPE         "Release")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -Wall -g")

find_package(catkin REQUIRED COMPONENTS
  roscpp
  tf
  sensor_msgs
  gazebo_ros
  rospy
  visualization_msgs
  pcl_ros
  pcl_conversions
  cv_bridge
  geometry_msgs
  nav_msgs
  std_msgs
  std_srvs
  control_msgs
  gazebo_msgs
  gazebo_plugins
)

find_package(gazebo REQUIRED COMPONENTS
  sensors
  physics
  common
  transport
  msgs
  plugins
)

find_package(Boost       REQUIRED COMPONENTS system thread filesystem)
find_package(OpenCV      REQUIRED)
find_package(Eigen3      REQUIRED)
find_package(PCL         1.7      REQUIRED)
find_package(Protobuf    REQUIRED)
find_package(ODE         REQUIRED)

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${GAZEBO_INCLUDE_DIRS}
  ${Boost_INCLUDE_DIRS}
  ${Protobuf_INCLUDE_DIRS}
  ${ODE_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
  ${EIGEN3_INCLUDE_DIRS}
  ${PCL_INCLUDE_DIRS}
)

#--------------------------------------------------
# 链接库目录（包括 Gazebo 插件目录）
#--------------------------------------------------
link_directories(
  ${GAZEBO_LIBRARY_DIRS}
  ${ODE_LIBRARY_DIRS}
  /usr/lib/x86_64-linux-gnu/gazebo-11/plugins
)
find_library(RAYPLUGIN_LIB
  NAMES RayPlugin
  HINTS ${GAZEBO_LIBRARY_DIRS} $ENV{GAZEBO_PLUGIN_PATH}
)

catkin_package(
  INCLUDE_DIRS include
  LIBRARIES    livox_laser_simulation realsense_gazebo_plugin
  CATKIN_DEPENDS
    roscpp tf gazebo_ros sensor_msgs pcl_ros pcl_conversions cv_bridge
    geometry_msgs nav_msgs std_msgs std_srvs control_msgs gazebo_msgs gazebo_plugins
  DEPENDS
    OpenCV Eigen3 Boost PCL Protobuf ODE
)
add_library(livox_laser_simulation SHARED
  src/livox/livox_points_plugin.cpp
  src/livox/livox_ode_multiray_shape.cpp
)
target_link_libraries(livox_laser_simulation
  ${catkin_LIBRARIES}
  ${GAZEBO_LIBRARIES}
  ${RAYPLUGIN_LIB}
  ${Boost_LIBRARIES}
  ${Protobuf_LIBRARIES}
  ${ODE_LIBRARIES}
  dl pthread
  OpenMP::OpenMP_CXX
)
set_target_properties(livox_laser_simulation PROPERTIES
  CXX_VISIBILITY_PRESET default
  VISIBILITY_INLINES_HIDDEN OFF
)
add_library(realsense_gazebo_plugin SHARED
  src/realsense/RealSensePlugin.cpp
  src/realsense/gazebo_ros_realsense.cpp
)
target_link_libraries(realsense_gazebo_plugin
  ${catkin_LIBRARIES}
  ${GAZEBO_LIBRARIES}
)
add_dependencies(realsense_gazebo_plugin ${catkin_EXPORTED_TARGETS})

install(TARGETS
  livox_laser_simulation
  realsense_gazebo_plugin
  ARCHIVE DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
  RUNTIME DESTINATION ${CATKIN_GLOBAL_BIN_DESTINATION}
)

install(DIRECTORY include/
  DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION}
)

