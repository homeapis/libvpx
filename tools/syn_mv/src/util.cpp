#include <memory>
#include <fstream>
#include <sstream>
#include <iostream>
#include <vector>
#include "util.h"

GLuint load_shaders(const char *vertex_file_path,
                    const char *fragment_file_path) {
  // Create the shaders
  GLuint vertex_shader_ID = glCreateShader(GL_VERTEX_SHADER);
  GLuint fragment_shader_ID = glCreateShader(GL_FRAGMENT_SHADER);

  // Read the Vertex Shader code from the file
  std::string vertex_shader_code;
  std::ifstream vertex_shader_stream(vertex_file_path, std::ios::in);
  if (vertex_shader_stream.is_open()) {
    std::stringstream sstr;
    sstr << vertex_shader_stream.rdbuf();
    vertex_shader_code = sstr.str();
    vertex_shader_stream.close();
  } else {
    std::cout << "Impossible to open " << vertex_file_path;
    return 0;
  }

  // Read the Fragment Shader code from the file
  std::string fragment_shader_code;
  std::ifstream fragment_shader_stream(fragment_file_path, std::ios::in);
  if (fragment_shader_stream.is_open()) {
    std::stringstream sstr;
    sstr << fragment_shader_stream.rdbuf();
    fragment_shader_code = sstr.str();
    fragment_shader_stream.close();
  }

  GLint result = GL_FALSE;
  int info_log_length;

  // Compile Vertex Shader
  printf("Compiling shader : %s\n", vertex_file_path);
  char const *VertexSourcePointer = vertex_shader_code.c_str();
  glShaderSource(vertex_shader_ID, 1, &VertexSourcePointer, NULL);
  glCompileShader(vertex_shader_ID);

  // Check Vertex Shader
  glGetShaderiv(vertex_shader_ID, GL_COMPILE_STATUS, &result);
  glGetShaderiv(vertex_shader_ID, GL_INFO_LOG_LENGTH, &info_log_length);
  if (info_log_length > 0) {
    std::vector<char> vertext_shader_error_message(info_log_length + 1);
    glGetShaderInfoLog(vertex_shader_ID, info_log_length, NULL,
                       &vertext_shader_error_message[0]);
    printf("%s\n", &vertext_shader_error_message[0]);
  }

  // Compile Fragment Shader
  printf("Compiling shader : %s\n", fragment_file_path);
  char const *FragmentSourcePointer = fragment_shader_code.c_str();
  glShaderSource(fragment_shader_ID, 1, &FragmentSourcePointer, NULL);
  glCompileShader(fragment_shader_ID);

  // Check Fragment Shader
  glGetShaderiv(fragment_shader_ID, GL_COMPILE_STATUS, &result);
  glGetShaderiv(fragment_shader_ID, GL_INFO_LOG_LENGTH, &info_log_length);
  if (info_log_length > 0) {
    std::vector<char> fragment_shader_error_message(info_log_length + 1);
    glGetShaderInfoLog(fragment_shader_ID, info_log_length, NULL,
                       &fragment_shader_error_message[0]);
    printf("%s\n", &fragment_shader_error_message[0]);
  }

  // Link the program
  printf("Linking program\n");
  GLuint program_ID = glCreateProgram();
  glAttachShader(program_ID, vertex_shader_ID);
  glAttachShader(program_ID, fragment_shader_ID);
  glLinkProgram(program_ID);

  // Check the program
  glGetProgramiv(program_ID, GL_LINK_STATUS, &result);
  glGetProgramiv(program_ID, GL_INFO_LOG_LENGTH, &info_log_length);
  if (info_log_length > 0) {
    std::vector<char> program_error_message(info_log_length + 1);
    glGetProgramInfoLog(program_ID, info_log_length, NULL,
                        &program_error_message[0]);
    printf("%s\n", &program_error_message[0]);
  }

  glDetachShader(program_ID, vertex_shader_ID);
  glDetachShader(program_ID, fragment_shader_ID);

  glDeleteShader(vertex_shader_ID);
  glDeleteShader(fragment_shader_ID);

  return program_ID;
}
