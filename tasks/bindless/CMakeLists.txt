add_executable(bindless
  main.cpp
  App.cpp
  Renderer.cpp
  WorldRenderer.cpp
  Bindless.cpp
)

target_link_libraries(bindless
  PRIVATE glfw etna glm::glm wsi gui scene render_utils)

target_add_shaders(bindless
  shaders/collect.comp
  shaders/render.frag
  shaders/render.vert
  shaders/fxaa.frag
  shaders/fxaa.vert
)
