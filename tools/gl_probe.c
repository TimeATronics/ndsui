// Minimal GL bring-up probe for the Brick (matches n64ui's known-good path).
#include <stdio.h>
#include <SDL.h>
#include <EGL/egl.h>

int main(void) {
  SDL_Init(SDL_INIT_VIDEO);
  printf("driver: %s\n", SDL_GetCurrentVideoDriver());
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_Window* w = SDL_CreateWindow("probe", 0, 0, 640, 480,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  printf("window: %p (%s)\n", (void*)w, SDL_GetError());
  if (!w) return 1;
  SDL_GLContext c = SDL_GL_CreateContext(w);
  printf("context: %p (%s)\n", (void*)c, SDL_GetError());
  if (!c) return 1;
  SDL_GL_MakeCurrent(w, c);
  printf("SDL_GL_GetProcAddress(glClear): %p\n",
         (void*)SDL_GL_GetProcAddress("glClear"));
  printf("SDL_GL_GetProcAddress(glGenBuffers): %p\n",
         (void*)SDL_GL_GetProcAddress("glGenBuffers"));
  printf("eglGetProcAddress(glClear): %p\n", (void*)eglGetProcAddress("glClear"));
  printf("version: %s\n", (const char*)SDL_GL_GetProcAddress("glGetString")
                              ? ((const char* (*)(unsigned))SDL_GL_GetProcAddress("glGetString"))(0x1F02)
                              : "?");
  SDL_GL_DeleteContext(c);
  SDL_DestroyWindow(w);
  return 0;
}
