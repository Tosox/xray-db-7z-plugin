// The reference xray_re-tools code reports failed assertions through this
// function. The tests only feed it valid data, so reaching it is a test failure.

#include <stdio.h>
#include <stdlib.h>

namespace xray_re {

void die(const char *message, const char *file, unsigned line)
{
  fprintf(stderr, "xray_re reference code failed: %s (%s:%u)\n", message, file, line);
  abort();
}

} // namespace xray_re
