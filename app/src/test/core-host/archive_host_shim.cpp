// archive.cpp calls std::ftell/std::fseek, which util/fs.h turns into the MinGW-only
// _ftelli64/_fseeki64 macros on Windows hosts. Declare them in std so the file builds unchanged.
#include <cstdio>
#include <util/fs.h>
namespace std {
using ::_fseeki64;
using ::_ftelli64;
}
#include "../../main/cpp/emucorev/src/savestate/archive.cpp"
