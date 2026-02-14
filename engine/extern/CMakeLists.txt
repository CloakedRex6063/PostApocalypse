CPMAddPackage(
        NAME GLFW
        GITHUB_REPOSITORY glfw/glfw
        GIT_TAG 3.4
        OPTIONS
        "GLFW_BUILD_TESTS OFF"
        "GLFW_BUILD_EXAMPLES OFF"
        "GLFW_BUILD_DOCS OFF"
)
CPMAddPackage(
        NAME MeshOpt
        GITHUB_REPOSITORY zeux/meshoptimizer
        GIT_TAG v1.0
)
CPMAddPackage(
        NAME GLM
        GITHUB_REPOSITORY g-truc/glm
        GIT_TAG 1.0.3
)
CPMAddPackage(
        NAME SWIFT
        GITHUB_REPOSITORY CloakedRex6063/SwiftRender
        GIT_TAG 328fe970
)
CPMAddPackage(
        NAME fastgltf
        GITHUB_REPOSITORY spnda/fastgltf
        GIT_TAG v0.9.0
)
CPMAddPackage(
        NAME DDS
        GITHUB_REPOSITORY turanszkij/dds
        GIT_TAG d48a0bc
)
CPMAddPackage(
        NAME imgui
        VERSION 1.92.5-docking
        GITHUB_REPOSITORY ocornut/imgui
        DOWNLOAD_ONLY TRUE
)
file(
        DOWNLOAD
        "https://gist.githubusercontent.com/rokups/f771217b2d530d170db5cb1e08e9a8f4/raw/4c2c14374ab878ca2f45daabfed4c156468e4e27/CMakeLists.txt"
        "${imgui_SOURCE_DIR}/CMakeLists.txt"
        EXPECTED_HASH SHA256=fd62f69364ce13a4f7633a9b50ae6672c466bcc44be60c69c45c0c6e225bb086
)
set(IMGUI_IMPL_GLFW TRUE)
set(IMGUI_IMPL_SDL FALSE)
set(IMGUI_IMPL_WIN32 TRUE)
set(IMGUI_EXAMPLES FALSE)
set(IMGUI_DEMO FALSE)
set(IMGUI_ENABLE_FREETYPE FALSE)
set(IMGUI_ENABLE_STDLIB_SUPPORT TRUE)
set(IMGUI_IMPL_GLUT FALSE)
set(IMGUI_IMPL_DX9 FALSE)
set(IMGUI_IMPL_DX10 FALSE)
set(IMGUI_IMPL_DX11 TRUE)
set(IMGUI_IMPL_DX12 TRUE)
set(IMGUI_IMPL_VULKAN FALSE)
add_subdirectory(${imgui_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/imgui EXCLUDE_FROM_ALL SYSTEM)

add_library(dds INTERFACE)
target_include_directories(dds
        INTERFACE
        ${DDS_SOURCE_DIR}
)
CPMAddPackage(
        NAME mikktspace
        GITHUB_REPOSITORY mmikk/MikkTSpace
        GIT_TAG master
        DOWNLOAD_ONLY TRUE
)
if (mikktspace_ADDED)
    add_library(mikktspace STATIC ${mikktspace_SOURCE_DIR}/mikktspace.c)
    target_include_directories(mikktspace PUBLIC ${mikktspace_SOURCE_DIR})
endif ()
CPMAddPackage(
        NAME stb
        GITHUB_REPOSITORY nothings/stb
        GIT_TAG master
)

if(stb_ADDED)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

target_link_libraries(Engine PUBLIC glfw meshoptimizer glm imgui imgui-dx12 imgui-glfw Swift dds fastgltf::fastgltf mikktspace stb)