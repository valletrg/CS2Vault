#!/bin/bash

PROJECT_DIR="${1:-/home/val/CLionProjects/untitled}"

if [ ! -d "$PROJECT_DIR" ]; then
  echo "Error: Directory '$PROJECT_DIR' not found."
  exit 1
fi

cd "$PROJECT_DIR" || exit 1

echo "Cleaning markdown artifacts in: $PROJECT_DIR"

# Match all common markdown artifacts:
# - ```cpp, ```c, ```h, ```bash, ``` alone, etc.
# - Lines like "The changes are extensive", "Here is the updated...", etc.
PATTERNS=(
  '^```'           # any opening/closing backtick fence
  '^\s*```'        # fenced with leading whitespace
  'The changes are extensive'
  'Here is the updated'
  'Here are the changes'
  'The updated code'
  'Here'\''s the'
  '^---$'          # markdown horizontal rules
)

# Build a single sed expression from all patterns
SED_EXPR=""
for pattern in "${PATTERNS[@]}"; do
  SED_EXPR+="/${pattern}/d;"
done

CLEANED=0

for f in *.cpp *.h *.c *.cc *.cxx; do
  [ -f "$f" ] || continue  # skip if glob doesn't match

  if grep -qE "^\`\`\`" "$f" 2>/dev/null; then
    sed -i -E "$SED_EXPR" "$f"
    echo "  Cleaned: $f"
    ((CLEANED++))
  fi
done

echo "Done. $CLEANED file(s) cleaned."
