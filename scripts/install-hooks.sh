#!/bin/sh
# Installs a git "pre-commit hook": every time you (or your AI) commit, the safety check
# runs first and blocks the commit if it finds Elektron firmware, keys, or huge files.
#
# Usage (run from anywhere inside the repo):
#   sh install-hooks.sh                    -> expects the checker at scripts/check_safety.py
#   sh install-hooks.sh maintainer/check_safety.py   -> expects it at that path instead
set -e

CHECKER="${1:-scripts/check_safety.py}"
ROOT="$(git rev-parse --show-toplevel)"
HOOK="$ROOT/.git/hooks/pre-commit"

if [ ! -f "$ROOT/$CHECKER" ]; then
  echo "I can't find $CHECKER inside the repo. Copy check_safety.py there first."
  exit 1
fi

cat > "$HOOK" <<EOF
#!/bin/sh
# Installed by install-hooks.sh. Blocks commits that contain Elektron firmware or secrets.
ROOT="\$(git rev-parse --show-toplevel)"
PY="\$(command -v python3 || command -v python)"
if [ -z "\$PY" ]; then
  echo "Python not found, so the safety check could not run. Commit blocked to be safe."
  exit 1
fi
"\$PY" "\$ROOT/$CHECKER" --staged || {
  echo
  echo "Commit blocked by the safety check. Ask your AI to explain what it found."
  exit 1
}
EOF

chmod +x "$HOOK"
echo "Safety hook installed at $HOOK (uses $CHECKER)."
