#ifndef METAL_UTILITY_H
#define METAL_UTILITY_H

typedef struct GLFWwindow GLFWwindow;

#ifdef __cplusplus
extern "C"
{
#endif

    void* GetMetalLayer(GLFWwindow* window);

#ifdef __cplusplus
}
#endif

#endif
