/* stb_image (public domain / MIT, https://github.com/nothings/stb) - JPEG + PNG only,
 * decoded from memory. Header is fetched by tools/get_deps.sh. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb_image.h"
