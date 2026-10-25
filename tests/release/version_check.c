/* For CI's release dry run, against a stamped nanosemver.h: NSV_VERSION_LIT formats to
   exactly NSV_VERSION, so the parts and the whole were stamped alike. */
#include "nanosemver.h"

#include <stdio.h>
#include <string.h>

static nsv_semver_t const k_version = NSV_VERSION_LIT;

int main(void) {
  char buf[256];
  if ((nsv_format(buf, sizeof buf, &k_version) < 0) || strcmp(buf, NSV_VERSION)) {
    fprintf(stderr, "NSV_VERSION_LIT is %s, NSV_VERSION is %s\n", buf, NSV_VERSION);
    return 1;
  }
  printf("NSV_VERSION_LIT: %s\n", buf);
  return 0;
}
