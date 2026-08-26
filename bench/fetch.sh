#!/bin/sh
# Clones or updates the corpus, which lives in binary2textbench.
#
#     bench/fetch.sh              # just the checkout; the scripts fetch what they need
#     bench/fetch.sh core         # ... and materialise the core group now
#     bench/fetch.sh all
#
# bench/corpus.py and bench/wire_samples.py used to be here. See bench/central.py
# for why they are not.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo=${B2TB_DIR:-$here/.b2tb}

if [ ! -d "$repo/corpus" ]; then
    echo "cloning binary2textbench into $repo" >&2
    git clone --depth 1 https://github.com/keywan-ghadami/binary2textbench "$repo"
elif [ -d "$repo/.git" ]; then
    git -C "$repo" pull --ff-only --quiet || \
        echo "note: could not update $repo, using what is there" >&2
fi

if [ $# -gt 0 ]; then
    groups=$1
    [ "$groups" = "all" ] && groups=core,short,synthetic,silesia
    python3 "$repo/corpus/manifest.py" --groups="$groups"

    # speed/Makefile and speed/binary_flag_instructions.sh read bench/corpus/
    # directly, so give them one where they expect it.
    mkdir -p "$here/corpus"
    for entry in "$repo"/corpus/data/*; do
        name=$(basename "$entry")
        # Bookkeeping, not samples: linking these in would measure them.
        case "$name" in _archives|manifest.json) continue ;; esac
        ln -sfn "$entry" "$here/corpus/$name"
    done
fi

echo "corpus available at $repo/corpus" >&2
