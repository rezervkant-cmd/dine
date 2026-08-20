#pragma once
#include <cstddef>
#include <cstdint>

using GLenum = unsigned int;  using GLuint = unsigned int;  using GLint = int;
using GLsizei = int;          using GLboolean = unsigned char;
using GLbitfield = unsigned int; using GLfloat = float;
using GLchar = char;          using GLsizeiptr = std::ptrdiff_t;
using GLintptr = std::ptrdiff_t; using GLvoid = void;

#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_DEPTH_TEST         0x0B71
#define GL_CULL_FACE          0x0B44
#define GL_BLEND              0x0BE2
#define GL_SRC_ALPHA          0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_TRIANGLES          0x0004
#define GL_UNSIGNED_INT       0x1405
#define GL_FLOAT              0x1406
#define GL_UNSIGNED_SHORT     0x1403
#define GL_ARRAY_BUFFER       0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW        0x88E4
#define GL_VERTEX_SHADER      0x8B31
#define GL_FRAGMENT_SHADER    0x8B30
#define GL_COMPILE_STATUS     0x8B81
#define GL_LINK_STATUS        0x8B82
#define GL_TEXTURE_2D         0x0DE1
#define GL_TEXTURE0           0x84C0
#define GL_RGBA8              0x8058
#define GL_RGBA               0x1908
#define GL_UNSIGNED_BYTE      0x1401
#define GL_NEAREST            0x2600
#define GL_LINEAR             0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_NEAREST_MIPMAP_LINEAR  0x2702
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803
#define GL_CLAMP_TO_EDGE      0x812F
#define GL_TEXTURE_MAX_LEVEL  0x813D
#define GL_MULTISAMPLE        0x809D

#define VW_GL_FUNCS(X) \
    X(void,  glEnable, GLenum) \
    X(void,  glDisable, GLenum) \
    X(void,  glClear, GLbitfield) \
    X(void,  glClearColor, GLfloat, GLfloat, GLfloat, GLfloat) \
    X(void,  glViewport, GLint, GLint, GLsizei, GLsizei) \
    X(void,  glBlendFunc, GLenum, GLenum) \
    X(void,  glDrawElements, GLenum, GLsizei, GLenum, const void*) \
    X(GLuint, glCreateShader, GLenum) \
    X(void,  glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*) \
    X(void,  glCompileShader, GLuint) \
    X(void,  glGetShaderiv, GLuint, GLenum, GLint*) \
    X(void,  glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    X(GLuint, glCreateProgram) \
    X(void,  glAttachShader, GLuint, GLuint) \
    X(void,  glLinkProgram, GLuint) \
    X(void,  glGetProgramiv, GLuint, GLenum, GLint*) \
    X(void,  glGetProgramInfoLog, GLuint, GLsizei, GLsizei*, GLchar*) \
    X(void,  glDeleteShader, GLuint) \
    X(void,  glUseProgram, GLuint) \
    X(GLint, glGetUniformLocation, GLuint, const GLchar*) \
    X(void,  glUniformMatrix4fv, GLint, GLsizei, GLboolean, const GLfloat*) \
    X(void,  glUniform1i, GLint, GLint) \
    X(void,  glUniform3fv, GLint, GLsizei, const GLfloat*) \
    X(void,  glGenVertexArrays, GLsizei, GLuint*) \
    X(void,  glBindVertexArray, GLuint) \
    X(void,  glGenBuffers, GLsizei, GLuint*) \
    X(void,  glBindBuffer, GLenum, GLuint) \
    X(void,  glBufferData, GLenum, GLsizeiptr, const void*, GLenum) \
    X(void,  glEnableVertexAttribArray, GLuint) \
    X(void,  glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) \
    X(void,  glVertexAttribIPointer, GLuint, GLint, GLenum, GLsizei, const void*) \
    X(void,  glDeleteBuffers, GLsizei, const GLuint*) \
    X(void,  glDeleteVertexArrays, GLsizei, const GLuint*) \
    X(void,  glGenTextures, GLsizei, GLuint*) \
    X(void,  glDeleteTextures, GLsizei, const GLuint*) \
    X(void,  glBindTexture, GLenum, GLuint) \
    X(void,  glActiveTexture, GLenum) \
    X(void,  glTexParameteri, GLenum, GLenum, GLint) \
    X(void,  glTexImage2D, GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) \
    X(void,  glGenerateMipmap, GLenum) \
    X(void,  glDepthMask, GLboolean)

#define VW_DECL(ret, name, ...) extern ret (*name)(__VA_ARGS__);
VW_GL_FUNCS(VW_DECL)
#undef VW_DECL

bool vw_gl_load(void* (*loader)(const char*));
