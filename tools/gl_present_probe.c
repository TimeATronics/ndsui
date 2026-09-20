// GL present probe: paints 4 colored quadrants + a white border, then holds
// the frame so fbscreencap can capture it. Reveals the driver's mapping.
#include <stdio.h>
#include <unistd.h>
#include <SDL.h>
#include <EGL/egl.h>

typedef unsigned (*PFNGLCREATESHADER)(unsigned);
typedef void (*PFNGLSHADERSOURCE)(unsigned, int, const char* const*, const int*);
typedef void (*PFNGLCOMPILESHADER)(unsigned);
typedef unsigned (*PFNGLCREATEPROGRAM)(void);
typedef void (*PFNGLATTACHSHADER)(unsigned, unsigned);
typedef void (*PFNGLLINKPROGRAM)(unsigned);
typedef void (*PFNGLUSEPROGRAM)(unsigned);
typedef int (*PFNGLGETUNIFORMLOCATION)(unsigned, const char*);
typedef void (*PFNGLUNIFORM4F)(int, float, float, float, float);
typedef void (*PFNGLVERTEXATTRIBPOINTER)(unsigned, int, unsigned, unsigned char, int, const void*);
typedef void (*PFNGLENABLEVERTEXATTRIBARRAY)(unsigned);
typedef void (*PFNGLDRAWARRAYS)(unsigned, int, int);
typedef void (*PFNGLVIEWPORT)(int, int, int, int);
typedef void (*PFNGLCLEARCOLOR)(float, float, float, float);
typedef void (*PFNGLCLEAR)(unsigned);
typedef void (*PFNGLDISABLE)(unsigned);

int main(void) {
  SDL_Init(SDL_INIT_VIDEO);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_Window* w = SDL_CreateWindow("glprobe", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, 1024, 768,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  SDL_GLContext c = SDL_GL_CreateContext(w);
  SDL_GL_MakeCurrent(w, c);
  printf("swapinterval set: %d\n", SDL_GL_SetSwapInterval(0));

  PFNGLVIEWPORT Viewport = (PFNGLVIEWPORT)SDL_GL_GetProcAddress("glViewport");
  PFNGLCLEARCOLOR ClearColor = (PFNGLCLEARCOLOR)SDL_GL_GetProcAddress("glClearColor");
  PFNGLCLEAR Clear = (PFNGLCLEAR)SDL_GL_GetProcAddress("glClear");
  PFNGLDISABLE Disable = (PFNGLDISABLE)SDL_GL_GetProcAddress("glDisable");
  PFNGLCREATESHADER CreateShader = (PFNGLCREATESHADER)SDL_GL_GetProcAddress("glCreateShader");
  PFNGLSHADERSOURCE ShaderSource = (PFNGLSHADERSOURCE)SDL_GL_GetProcAddress("glShaderSource");
  PFNGLCOMPILESHADER CompileShader = (PFNGLCOMPILESHADER)SDL_GL_GetProcAddress("glCompileShader");
  PFNGLCREATEPROGRAM CreateProgram = (PFNGLCREATEPROGRAM)SDL_GL_GetProcAddress("glCreateProgram");
  PFNGLATTACHSHADER AttachShader = (PFNGLATTACHSHADER)SDL_GL_GetProcAddress("glAttachShader");
  PFNGLLINKPROGRAM LinkProgram = (PFNGLLINKPROGRAM)SDL_GL_GetProcAddress("glLinkProgram");
  PFNGLUSEPROGRAM UseProgram = (PFNGLUSEPROGRAM)SDL_GL_GetProcAddress("glUseProgram");
  PFNGLGETUNIFORMLOCATION GetUniformLocation = (PFNGLGETUNIFORMLOCATION)SDL_GL_GetProcAddress("glGetUniformLocation");
  PFNGLUNIFORM4F Uniform4f = (PFNGLUNIFORM4F)SDL_GL_GetProcAddress("glUniform4f");
  PFNGLVERTEXATTRIBPOINTER VertexAttribPointer = (PFNGLVERTEXATTRIBPOINTER)SDL_GL_GetProcAddress("glVertexAttribPointer");
  PFNGLENABLEVERTEXATTRIBARRAY EnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAY)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
  PFNGLDRAWARRAYS DrawArrays = (PFNGLDRAWARRAYS)SDL_GL_GetProcAddress("glDrawArrays");

  const char* vs = "attribute vec2 p; attribute vec3 col; varying vec3 vc;"
                   "void main(){ vc=col; gl_Position=vec4(p,0.0,1.0);}";
  const char* fs = "precision mediump float; varying vec3 vc;"
                   "void main(){ gl_FragColor=vec4(vc,1.0);}";
  unsigned v = CreateShader(0x8B31), f = CreateShader(0x8B30);
  ShaderSource(v, 1, &vs, 0); CompileShader(v);
  ShaderSource(f, 1, &fs, 0); CompileShader(f);
  unsigned prog = CreateProgram();
  AttachShader(prog, v); AttachShader(prog, f); LinkProgram(prog);
  UseProgram(prog);

  // fullscreen quad: pos + color (TL red, TR green, BL blue, BR yellow)
  float verts[] = {
    -1, 1, 1, 0, 0,   1, 1, 0, 1, 0,
    -1, -1, 0, 0, 1,  1, -1, 1, 1, 0,
  };
  EnableVertexAttribArray(0);
  VertexAttribPointer(0, 2, 0x1406, 0, 5 * 4, verts);
  EnableVertexAttribArray(1);
  VertexAttribPointer(1, 3, 0x1406, 0, 5 * 4, verts + 2);

  for (int i = 0; i < 200; ++i) {
    Viewport(0, 0, 1024, 768);
    ClearColor(0.1f, 0.1f, 0.1f, 1);
    Clear(0x4000);
    Disable(0x0B71);
    UseProgram(prog);
    DrawArrays(0x0005, 0, 4);
    SDL_GL_SwapWindow(w);
    usleep(50000);
  }
  (void)GetUniformLocation; (void)Uniform4f;
  return 0;
}
