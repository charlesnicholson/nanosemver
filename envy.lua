-- nanosemver's toolchain manifest, and envy's root marker. Only the unit tests need
-- anything: doctest.h. The cache lives under build/ with everything else this repo
-- generates; `make clean` spares it, `rm -rf build` does not.

-- @envy schema "1"
-- @envy version "0.4.12"
-- @envy sha256sums "89c88b2aa2c175d60e949e32b8f9986289e0659876eb5017670fc46469fd7ec9"
-- @envy bin "bin"
-- @envy cache-local "build/envy-cache"
-- @envy deploy "true"
-- @envy root "true"

BUNDLES = {
  ["envy"] = {
    identity = "envy.package-specs@r10",
    source = "https://github.com/envy-package-manager/package-specs.git",
    ref = "baff2f5ab12a4d2bb20c2365a5ad370c3a0eedd5",
  },
}

PACKAGES = {
  { spec = "envy.doctest-cpp@r0", bundle = "envy", options = { version = "2.5.3" } },
}
