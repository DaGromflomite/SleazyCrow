#!/usr/bin/env bash
# One-time publish of Sleazy Crow as a proper fork. See RELEASING.md section A.
# Stops at the first error. Nothing public happens before you type PUBLISH.
set -euo pipefail

OWNER=DaGromflomite
REPO=SleazyCrow
FULL="$OWNER/$REPO"
UPSTREAM=Senape3000/EvilCrowRF-V2
SIGNING_DIR="/c/Users/Timot/SleazyCrow-signing"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

step() { printf '\n== %s\n' "$*"; }

step "Pre-flight"
[ "$(git branch --show-current)" = "sleazycrow" ] || { echo "switch to branch sleazycrow first"; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "commit or stash local changes first"; exit 1; }
python tools/sleazy.py check --release
VERSION=$(python tools/sleazy.py status | sed -n 's/^firmware: \([^ ]*\).*/\1/p')
TAG="v$VERSION"
[ -f "$SIGNING_DIR/sleazycrow-release.p12" ] || { echo "signing key missing in $SIGNING_DIR"; exit 1; }
PASS=$(sed -n 's/^password: \([^ ]*\).*/\1/p' "$SIGNING_DIR/PASSWORD-KEEP-SECRET.txt")
[ -n "$PASS" ] || { echo "could not read signing password"; exit 1; }
gh auth status >/dev/null
git rev-parse -q --verify "refs/tags/$TAG" >/dev/null && { echo "tag $TAG already exists locally"; exit 1; }

echo
echo "About to publish Sleazy Crow $VERSION as https://github.com/$FULL (fork of $UPSTREAM)."
read -r -p "Type PUBLISH to continue: " ok
[ "$ok" = "PUBLISH" ] || { echo "cancelled, nothing changed"; exit 1; }

step "1. Fork $UPSTREAM as $FULL"
if gh repo view "$FULL" >/dev/null 2>&1; then
  echo "$FULL already exists"
else
  gh repo fork "$UPSTREAM" --fork-name "$REPO" --clone=false
  for _ in $(seq 1 30); do gh repo view "$FULL" >/dev/null 2>&1 && break; sleep 2; done
fi
[ "$(gh repo view "$FULL" --json isFork --jq .isFork)" = "true" ] || { echo "$FULL is not a fork"; exit 1; }

step "2. Description, homepage, Issues, Actions"
gh repo edit "$FULL" --enable-issues \
  --description "Sleazy Crow: community fork of Senape3000's EvilCrowRF-V2 firmware + app for the Evil Crow RF V2 (beta)" \
  --homepage "https://dagromflomite.github.io/$REPO/"
gh api -X PUT "repos/$FULL/actions/permissions" -F enabled=true -f allowed_actions=all >/dev/null

step "3. Push code to main"
git remote get-url sleazy >/dev/null 2>&1 || git remote add sleazy "https://github.com/$FULL.git"
git push sleazy sleazycrow:main

step "4. Signing key -> encrypted Actions secrets"
base64 -w0 "$SIGNING_DIR/sleazycrow-release.p12" | gh secret set SLEAZY_KEYSTORE_B64 -R "$FULL"
printf '%s' "$PASS" | gh secret set SLEAZY_KEYSTORE_PASSWORD -R "$FULL"

step "5. GitHub Pages from Actions (and allow version tags to deploy)"
gh api -X POST "repos/$FULL/pages" -f build_type=workflow >/dev/null 2>&1 \
  || gh api -X PUT "repos/$FULL/pages" -f build_type=workflow >/dev/null
gh api -X PUT "repos/$FULL/environments/github-pages" \
  --input - >/dev/null <<'JSON'
{"deployment_branch_policy": {"protected_branches": false, "custom_branch_policies": true}}
JSON
gh api -X POST "repos/$FULL/environments/github-pages/deployment-branch-policies" -f name=main -f type=branch >/dev/null 2>&1 || true
gh api -X POST "repos/$FULL/environments/github-pages/deployment-branch-policies" -f name='v*' -f type=tag >/dev/null 2>&1 || true

step "6. Tag $TAG and start the release pipeline"
git tag -a "$TAG" -m "Sleazy Crow $VERSION"
git push sleazy "$TAG"

step "7. Wait for the pipeline"
sleep 15
RUN=$(gh run list -R "$FULL" --workflow release.yml --limit 1 --json databaseId --jq '.[0].databaseId')
gh run watch "$RUN" -R "$FULL" --exit-status

step "8. Verify the published files"
TMP=$(mktemp -d)
gh release download "$TAG" -R "$FULL" -D "$TMP"
( cd "$TMP" && for m in *.md5; do md5sum -c "$m"; done )
echo
echo "Published: https://github.com/$FULL/releases/tag/$TAG"
echo "Flasher:   https://dagromflomite.github.io/$REPO/"
echo "Next: check the flasher page, test the APK's update check, archive the old repo (RELEASING.md A)."
