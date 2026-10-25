/* For CI's release dry run: a stamped NSV_VERSION_LIT formats to exactly the tag's version,
   given as the one argument. */
#include "nanosemver.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  nsv_semver_t const v = NSV_VERSION_LIT;
  char buf[256];
  if ((argc != 2) || (nsv_format(buf, sizeof buf, &v) < 0) || strcmp(buf, argv[1])) {
    fprintf(stderr, "NSV_VERSION_LIT is %s, not %s\n", buf, (argc == 2) ? argv[1] : "?");
    return 1;
  }
  printf("NSV_VERSION_LIT: %s\n", buf);
  return 0;
}
