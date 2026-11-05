#!/usr/bin/env bash
# Fails when the checked-in TypeScript types no longer match contracts/*.schema.json.
# It regenerates the types in place and compares them with what was there before, so
# unrelated uncommitted edits elsewhere in the tree do not matter.
set -euo pipefail
cd "$(dirname "$0")/.."
before=$(sha256sum web/src/api/generated.ts web/src/api/api-*.generated.ts | sort)
npm --prefix web run contract:generate >/dev/null
after=$(sha256sum web/src/api/generated.ts web/src/api/api-*.generated.ts | sort)
if [[ "$before" != "$after" ]]; then
  echo "Generated contract types were out of date and have been regenerated." >&2
  echo "Review and commit: npm --prefix web run contract:generate" >&2
  diff <(echo "$before") <(echo "$after") >&2 || true
  exit 1
fi
echo "Generated contract types match contracts/."
