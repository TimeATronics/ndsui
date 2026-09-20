// Minimal GL function loader + small helpers (shaders, textures, FBO).
// Works with desktop GL and GLES via SDL_GL_GetProcAddress.
#pragma once

#include <cstdint>
#include <string>

#include <SDL.h>

namespace ndsui {
namespace gl {

// function pointers (loaded once)
extern void (*GenBuffers)(int, unsigned*);
extern void (*BindBuffer)(unsigned, unsigned);
extern void (*BufferData)(unsigned, long, const void*, unsigned);
extern void (*DeleteBuffers)(int, const unsigned*);
extern void (*GenVertexArrays)(int, unsigned*);
extern void (*BindVertexArray)(unsigned);
extern void (*DeleteVertexArrays)(int, const unsigned*);
extern void (*EnableVertexAttribArray)(unsigned);
extern void (*DisableVertexAttribArray)(unsigned);
extern void (*VertexAttribPointer)(unsigned, int, unsigned, unsigned char, int,
                                   const void*);
extern unsigned (*CreateShader)(unsigned);
extern void (*ShaderSource)(unsigned, int, const char* const*, const int*);
extern void (*CompileShader)(unsigned);
extern void (*GetShaderiv)(unsigned, unsigned, int*);
extern void (*GetShaderInfoLog)(unsigned, int, int*, char*);
extern void (*DeleteShader)(unsigned);
extern unsigned (*CreateProgram)();
extern void (*AttachShader)(unsigned, unsigned);
extern void (*LinkProgram)(unsigned);
extern void (*GetProgramiv)(unsigned, unsigned, int*);
extern void (*GetProgramInfoLog)(unsigned, int, int*, char*);
extern void (*UseProgram)(unsigned);
extern void (*DeleteProgram)(unsigned);
extern int (*GetUniformLocation)(unsigned, const char*);
extern void (*UniformMatrix4fv)(int, int, unsigned char, const float*);
extern void (*Uniform1i)(int, int);
extern void (*Uniform1f)(int, float);
extern void (*Uniform3f)(int, float, float, float);
extern void (*Uniform4f)(int, float, float, float, float);
extern void (*GenTextures)(int, unsigned*);
extern void (*BindTexture)(unsigned, unsigned);
extern void (*TexImage2D)(unsigned, int, int, int, int, int, unsigned, unsigned,
                          const void*);
extern void (*TexParameteri)(unsigned, unsigned, int);
extern void (*DeleteTextures)(int, const unsigned*);
extern void (*ActiveTexture)(unsigned);
extern void (*GenFramebuffers)(int, unsigned*);
extern void (*BindFramebuffer)(unsigned, unsigned);
extern void (*FramebufferTexture2D)(unsigned, unsigned, unsigned, unsigned,
                                    int);
extern void (*DeleteFramebuffers)(int, const unsigned*);
extern unsigned (*CheckFramebufferStatus)(unsigned);
extern void (*Viewport)(int, int, int, int);
extern void (*ClearColor)(float, float, float, float);
extern void (*Clear)(unsigned);
extern void (*Enable)(unsigned);
extern void (*Disable)(unsigned);
extern void (*DepthFunc)(unsigned);
extern void (*DrawArrays)(unsigned, int, int);
extern void (*DrawElements)(unsigned, int, unsigned, const void*);
extern void (*PixelStorei)(unsigned, int);
extern void (*ReadPixels)(int, int, int, int, unsigned, unsigned, void*);
extern void (*BlendFunc)(unsigned, unsigned);
extern void (*GetIntegerv)(unsigned, int*);
extern unsigned (*GetError)();
extern void (*ValidateProgram)(unsigned);

bool load();  // true when the essentials resolved
bool available();

// ports for the few constants we need without including GL headers
constexpr unsigned TEX_2D = 0x0DE1;

constexpr unsigned RGBA = 0x1908;
constexpr unsigned UNSIGNED_BYTE = 0x1401;
constexpr unsigned UNSIGNED_SHORT = 0x1403;
constexpr unsigned LINEAR = 0x2601;
constexpr unsigned NEAREST = 0x2600;
constexpr unsigned CLAMP_EDGE = 0x812F;
constexpr unsigned FRAMEBUFFER = 0x8D40;
constexpr unsigned COLOR_ATTACHMENT0 = 0x8CE0;
constexpr unsigned FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr unsigned DEPTH_TEST = 0x0B71;
constexpr unsigned LEQUAL = 0x0203;
constexpr unsigned BLEND = 0x0BE2;
constexpr unsigned SRC_ALPHA = 0x0302;
constexpr unsigned ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr unsigned ARRAY_BUFFER = 0x8892;
constexpr unsigned STATIC_DRAW = 0x88E4;
constexpr unsigned FLOAT = 0x1406;
constexpr unsigned TRIANGLES = 0x0004;
constexpr unsigned UNSIGNED_INT = 0x1405;
constexpr unsigned COLOR_BUFFER_BIT = 0x00004000;
constexpr unsigned DEPTH_BUFFER_BIT = 0x00000100;
constexpr unsigned VERTEX_SHADER = 0x8B31;
constexpr unsigned FRAGMENT_SHADER = 0x8B30;
constexpr unsigned COMPILE_STATUS = 0x8B81;
constexpr unsigned LINK_STATUS = 0x8B82;

}  // namespace gl
}  // namespace ndsui
