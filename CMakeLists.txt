cmake_minimum_required(VERSION 3.14)
project(Terrain)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(Terrain main.c noise.c)

target_include_directories(Terrain PUBLIC ${SDL2_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR})
target_compile_features(Terrain PUBLIC c_std_99)
target_link_libraries(Terrain ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES})
