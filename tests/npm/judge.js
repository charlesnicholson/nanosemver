// Reads tests/npm/emit.cc's cases on stdin and holds each to npm's semver package, whose
// directory is $SEMVER: npm must accept every range nanosemver does, and answer the same,
// its includePrerelease against flags 0 and its default against NSV_RANGE_EXCLUDE_PRE.
// Exits nonzero on any disagreement but the two kinds npm's own limits and rules explain.
'use strict';
const semver = require(process.env.SEMVER);
const lines = require('fs').readFileSync(0, 'utf8').split('\n').filter(Boolean);

// npm's limits: 256-byte strings, and numbers up to 2^53 - 1.
const beyondNpm = s => (s.length > 256) || /\d{16,}/.test(s);
// npm reads >=0.0.0 as *, also as the lower bound of ^0.0.0 and ~0.0.0, which lets 0.0.0's
// pre-releases past it, and a set of just * takes over the whole range. Only under npm's
// default, NSV_RANGE_EXCLUDE_PRE's counterpart, and only for a pre-release.
const npmAny = (r, v) => (semver.prerelease(v) !== null) &&
    /(^|[\s|])(>=|\^|~)\s*0\.0\.0(\+[0-9A-Za-z.-]*)?(?=$|[\s|])/.test(r);

let checked = 0, beyond = 0, any = 0;
const failures = [];
for (const line of lines) {
  const [range, version, plain, excl] = JSON.parse(line);
  if (beyondNpm(range) || beyondNpm(version)) {
    beyond++;
    continue;
  }
  checked++;
  if (semver.validRange(range) === null) {
    failures.push(`npm rejects ${JSON.stringify(range)}`);
    continue;
  }
  const inc = semver.satisfies(version, range, { includePrerelease: true });
  if (inc !== (plain === 1)) {
    failures.push(`${JSON.stringify(range)} ${version}: flags 0 says ${plain}, ` +
                  `npm's includePrerelease ${inc}`);
  }
  const def = semver.satisfies(version, range);
  if (def !== (excl === 1)) {
    if (npmAny(range, version)) {
      any++;
    } else {
      failures.push(`${JSON.stringify(range)} ${version}: NSV_RANGE_EXCLUDE_PRE says ${excl}, ` +
                    `npm's default ${def}`);
    }
  }
}
console.log(`npm semver ${require(process.env.SEMVER + '/package.json').version}: ` +
            `${checked} cases, ${failures.length} disagree; ${any} differ only where npm ` +
            `reads >=0.0.0 as *; ${beyond} beyond npm's limits, skipped`);
for (const f of failures.slice(0, 20)) {
  console.log('  ' + f);
}
process.exit(failures.length ? 1 : 0);
