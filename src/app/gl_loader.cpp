#include "gl_loader.hpp"

#define VW_DEF(ret, name, ...) ret (*name)(__VA_ARGS__) = nullptr;
VW_GL_FUNCS(VW_DEF)
#undef VW_DEF

bool vw_gl_load(void* (*loader)(const char*)) {
    bool ok = true;
#define VW_LOAD(ret, name, ...) \
    name = reinterpret_cast<ret (*)(__VA_ARGS__)>(loader(#name)); \
    if (!name) ok = false;
    VW_GL_FUNCS(VW_LOAD)
#undef VW_LOAD
    return ok;
}
