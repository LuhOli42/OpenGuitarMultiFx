# Switches JUCE's Linux OpenGL backend from GLX to EGL + OpenGL ES when JUCE_OPENGL_LINUX_EGL=1 is defined (the
# Cubie A7S's PowerVR GPU has no hardware GLX, only EGL/GLES). Idempotent; run as FetchContent's PATCH_COMMAND and
# again from CMakeLists.txt so an already-populated _deps tree gets it too.
#   cmake -DJUCE_DIR=<juce source> -P JuceEglPatch.cmake
set(m "${JUCE_DIR}/modules/juce_opengl")

function(patch_file file from to)
  file(READ "${file}" text)
  string(FIND "${text}" "${to}" already)
  if(already GREATER -1)
    return()
  endif()
  string(FIND "${text}" "${from}" at)
  if(at EQUAL -1)
    message(FATAL_ERROR "JuceEglPatch: pattern not found in ${file}: ${from}")
  endif()
  string(REPLACE "${from}" "${to}" text "${text}")
  file(WRITE "${file}" "${text}")
endfunction()

patch_file("${m}/juce_opengl.h"
  "#if JUCE_IOS || JUCE_ANDROID\n #define JUCE_OPENGL_ES 1"
  "#if JUCE_IOS || JUCE_ANDROID || JUCE_OPENGL_LINUX_EGL\n #define JUCE_OPENGL_ES 1")

patch_file("${m}/juce_opengl.cpp"
  " #include <GL/glx.h>\n"
  " #if JUCE_OPENGL_LINUX_EGL\n  #include <EGL/egl.h>\n #else\n  #include <GL/glx.h>\n #endif\n")

patch_file("${m}/juce_opengl.cpp"
  " #include \"native/juce_OpenGL_linux.h\"\n"
  " #if JUCE_OPENGL_LINUX_EGL\n  #include \"native/juce_OpenGL_linux_egl.h\"\n #else\n  #include \"native/juce_OpenGL_linux.h\"\n #endif\n")

patch_file("${m}/juce_opengl.cpp"
  " #if JUCE_OPENGL_LINUX_EGL\n  #include <EGL/egl.h>\n #else"
  " #if JUCE_OPENGL_LINUX_EGL\n  #include <EGL/egl.h>\n  #include <dlfcn.h>\n #else")

patch_file("${m}/opengl/juce_OpenGLHelpers.cpp"
  "    return (void*) glXGetProcAddress ((const GLubyte*) functionName);\n"
  "   #if JUCE_OPENGL_LINUX_EGL\n    return (void*) eglGetProcAddress (functionName);\n   #else\n    return (void*) glXGetProcAddress ((const GLubyte*) functionName);\n   #endif\n")

configure_file("${CMAKE_CURRENT_LIST_DIR}/juce-egl/juce_OpenGL_linux_egl.h" "${m}/native/juce_OpenGL_linux_egl.h" COPYONLY)
