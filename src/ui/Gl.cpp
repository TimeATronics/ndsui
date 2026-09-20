#include "ui/Gl.h"

#include <dlfcn.h>

#include <EGL/egl.h>

#include "util/Log.h"

namespace ndsui {
namespace gl {

#define GL_FN(name) decltype(name) name = nullptr

GL_FN(GenBuffers);
GL_FN(BindBuffer);
GL_FN(BufferData);
GL_FN(DeleteBuffers);
GL_FN(GenVertexArrays);
GL_FN(BindVertexArray);
GL_FN(DeleteVertexArrays);
GL_FN(EnableVertexAttribArray);
GL_FN(DisableVertexAttribArray);
GL_FN(VertexAttribPointer);
GL_FN(CreateShader);
GL_FN(ShaderSource);
GL_FN(CompileShader);
GL_FN(GetShaderiv);
GL_FN(GetShaderInfoLog);
GL_FN(DeleteShader);
GL_FN(CreateProgram);
GL_FN(AttachShader);
GL_FN(LinkProgram);
GL_FN(GetProgramiv);
GL_FN(GetProgramInfoLog);
GL_FN(UseProgram);
GL_FN(DeleteProgram);
GL_FN(GetUniformLocation);
GL_FN(UniformMatrix4fv);
GL_FN(Uniform1i);
GL_FN(Uniform1f);
GL_FN(Uniform3f);
GL_FN(Uniform4f);
GL_FN(GenTextures);
GL_FN(BindTexture);
GL_FN(TexImage2D);
GL_FN(TexParameteri);
GL_FN(DeleteTextures);
GL_FN(ActiveTexture);
GL_FN(GenFramebuffers);
GL_FN(BindFramebuffer);
GL_FN(FramebufferTexture2D);
GL_FN(DeleteFramebuffers);
GL_FN(CheckFramebufferStatus);
GL_FN(Viewport);
GL_FN(ClearColor);
GL_FN(Clear);
GL_FN(Enable);
GL_FN(Disable);
GL_FN(DepthFunc);
GL_FN(DrawArrays);
GL_FN(DrawElements);
GL_FN(PixelStorei);
GL_FN(ReadPixels);
GL_FN(BlendFunc);
GL_FN(GetIntegerv);
GL_FN(GetError);
GL_FN(ValidateProgram);

namespace {
bool g_loaded = false;
bool g_ok = false;

template <typename T>
void get(T& fn, const char* name, bool& ok) {
  fn = (T)SDL_GL_GetProcAddress(name);
  if (!fn) fn = (T)eglGetProcAddress(name);      // Brick: real Mali entries
  if (!fn) fn = (T)dlsym(RTLD_DEFAULT, name);    // desktop fallback
  if (!fn) {
    LOG_ERROR("gl: missing %s", name);
    ok = false;
  }
}
}  // namespace

bool load() {
  if (g_loaded) return g_ok;
  g_loaded = true;
  bool ok = true;
#define GET(fn) get(fn, "gl" #fn, ok)
  GET(GenBuffers);
  GET(BindBuffer);
  GET(BufferData);
  GET(DeleteBuffers);
  GET(GenVertexArrays);
  GET(BindVertexArray);
  GET(DeleteVertexArrays);
  GET(EnableVertexAttribArray);
  GET(DisableVertexAttribArray);
  GET(VertexAttribPointer);
  GET(CreateShader);
  GET(ShaderSource);
  GET(CompileShader);
  GET(GetShaderiv);
  GET(GetShaderInfoLog);
  GET(DeleteShader);
  GET(CreateProgram);
  GET(AttachShader);
  GET(LinkProgram);
  GET(GetProgramiv);
  GET(GetProgramInfoLog);
  GET(UseProgram);
  GET(DeleteProgram);
  GET(GetUniformLocation);
  GET(UniformMatrix4fv);
  GET(Uniform1i);
  GET(Uniform1f);
  GET(Uniform3f);
  GET(Uniform4f);
  GET(GenTextures);
  GET(BindTexture);
  GET(TexImage2D);
  GET(TexParameteri);
  GET(DeleteTextures);
  GET(ActiveTexture);
  GET(GenFramebuffers);
  GET(BindFramebuffer);
  GET(FramebufferTexture2D);
  GET(DeleteFramebuffers);
  GET(CheckFramebufferStatus);
  GET(Viewport);
  GET(ClearColor);
  GET(Clear);
  GET(Enable);
  GET(Disable);
  GET(DepthFunc);
  GET(DrawArrays);
  GET(DrawElements);
  GET(PixelStorei);
  GET(ReadPixels);
  GET(BlendFunc);
  GET(GetIntegerv);
  GET(GetError);
  GET(ValidateProgram);
#undef GET
  g_ok = ok;
  if (!ok) LOG_WARN("gl: some entry points missing (3D disabled)");
  return ok;
}

bool available() { return g_ok; }

}  // namespace gl
}  // namespace ndsui
