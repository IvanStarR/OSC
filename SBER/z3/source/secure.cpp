#include "secmem/secure.hpp"
#include <cstddef>
#include <stdint.h>
#if __has_include(<openssl/crypto.h>)
#include <openssl/crypto.h>
#endif
#include <string.h>

namespace secmem {
void secure_zero(void* p, size_t n){
#if defined(OPENSSL_VERSION_NUMBER)
  OPENSSL_cleanse(p, n);
#elif defined(__STDC_LIB_EXT1__)
  memset_s(p, n, 0, n);
#else
  volatile unsigned char* v = (volatile unsigned char*)p;
  while(n--) *v++ = 0;
#endif
}
}
