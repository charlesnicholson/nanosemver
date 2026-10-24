-- @envy schema "1"
--
-- Node.js and npm's own semver package, for `make npm-check`, which holds nanosemver's
-- ranges to node-semver's. Fetched from nodejs.org and the npm registry. Not in the envy
-- bundle.

IDENTITY = "local.node@r0"
EXPORTABLE = false

local NODE = "24.22.1"
local SEMVER = "7.8.5"

local node_sha256 = {
  ["darwin-arm64"] = "654ba79de6155647e22321932b45f2a1ccb705976cc337c9b8e278beb085a628",
  ["darwin-x64"] = "65e450f90a6eaacd5000f611861c4ce65fadc87f813795dc4ed0c0e5f2fca2d4",
  ["linux-arm64"] = "dbabf7a1cbf947e6059ffad2c27b80041db97dea1e553b62afdee7f13fca336e",
  ["linux-x64"] = "6c172a9ab6718f7b801fb5a7bd486af2ee5e32758086a04b84fcce2aa77095e6",
  ["win-arm64"] = "a0c807319fede24c4a0d8f84564c82a619bd313b16b10c007c383c2609e99171",
  ["win-x64"] = "d2046d28c2c3dcd211ac5145c0bf30e2047ef9dfb306693c8c6e2eccd8b95fa5",
}
local semver_sha256 = "d85045d4300d7d57c891336b95df532e73f34c22ffcd222452b6d08b9d127d5d"

local function node_archive()
  local os = (envy.PLATFORM == "windows") and "win" or envy.PLATFORM
  local arch = (envy.ARCH == "x86_64") and "x64" or "arm64"
  local ext = (envy.PLATFORM == "windows") and "zip" or "tar.xz"
  return "node-v" .. NODE .. "-" .. os .. "-" .. arch .. "." .. ext, os .. "-" .. arch
end

FETCH = function(tmp_dir, opts)
  local archive, platform = node_archive()
  return {
    { source = "https://nodejs.org/dist/v" .. NODE .. "/" .. archive,
      sha256 = node_sha256[platform] },
    { source = "https://registry.npmjs.org/semver/-/semver-" .. SEMVER .. ".tgz",
      sha256 = semver_sha256 },
  }
end

STAGE = function(fetch_dir, stage_dir, tmp_dir, opts)
  envy.extract(fetch_dir .. "/" .. node_archive(), stage_dir .. "/node", { strip = 1 })
  envy.extract(fetch_dir .. "/semver-" .. SEMVER .. ".tgz", stage_dir .. "/semver",
               { strip = 1 })
end

PRODUCTS = {
  node = (envy.PLATFORM == "windows") and "node/node.exe" or "node/bin/node",
  node_semver = { value = "semver", script = false },
}
