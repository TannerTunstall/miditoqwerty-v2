#include "IInputBackend.h"

#ifdef _WIN32
  #include "win/WindowsInputBackend.h"
#elif defined(__APPLE__)
  #include "mac/MacInputBackend.h"
#endif

std::unique_ptr<IInputBackend> createInputBackend() {
#ifdef _WIN32
    return std::make_unique<WindowsInputBackend>();
#elif defined(__APPLE__)
    return std::make_unique<MacInputBackend>();
#else
  #error "No input backend for this platform"
#endif
}
