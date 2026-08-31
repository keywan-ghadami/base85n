#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Verify that every internal link in a built site resolves.

``site/build.py`` rewrites repository-relative Markdown links either to a
generated page or to a github.com URL. A typo in a link target, or a heading
that gets renamed without its anchor being updated, would otherwise only show
up as a 404 for a visitor. This checks both, on the generated HTML:

* every relative ``href`` points at a file that exists in the output;
* every ``#fragment`` matches an ``id`` on the page it points at.

External (``http``/``https``/``mailto:``/``tel:``/``data:``) links are not
fetched.

It also checks the *sources*, which the built site cannot catch. A Markdown
link to a repository file that is not one of the generated pages -- an example,
a workflow, a script -- is rewritten to an absolute github.com URL and is
therefore external by the time it reaches the output, so deleting the file it
points at leaves a 404 that every check above passes. So every relative
Markdown link in the repository is resolved against the working tree as well.

Usage: ``python3 site/check_links.py [BUILD_DIR]`` (default ``site/_build``).
"""

from __future__ import annotations

import html
import os
import re
import sys

LINK_RE = re.compile(r'<a\b[^>]*?\shref="([^"]*)"', re.IGNORECASE)
ID_RE = re.compile(r'\sid="([^"]+)"', re.IGNORECASE)
EXTERNAL_PREFIXES = ("http://", "https://", "mailto:", "tel:", "data:", "//")

# Inline Markdown links: [text](target), with an optional "title" and optional
# <> around the target. Reference-style links are not used in this repository.
MD_LINK_RE = re.compile(r"\]\(\s*<?([^)<>\s]+)>?(?:\s+[\"'][^)]*[\"'])?\s*\)")
# Fenced code blocks, stripped first: a shell snippet may contain something
# that reads as a link, and a command line is not a link.
FENCE_RE = re.compile(r"^```.*?^```", re.MULTILINE | re.DOTALL)
SKIP_DIRS = {".git", "node_modules", "_build", "_site", "target", "corpus",
             "dist", ".b2tb"}


def ids_in(path, cache):
    if path not in cache:
        with open(path, encoding="utf-8") as fh:
            cache[path] = set(ID_RE.findall(fh.read()))
    return cache[path]


def check_sources(root):
    """Every relative Markdown link in the tree points at a file that is there."""
    problems = []
    links = 0
    files = 0
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in sorted(dirnames) if d not in SKIP_DIRS]
        for filename in sorted(filenames):
            if not filename.endswith(".md"):
                continue
            files += 1
            path = os.path.join(dirpath, filename)
            with open(path, encoding="utf-8") as fh:
                text = FENCE_RE.sub("", fh.read())
            for target in MD_LINK_RE.findall(text):
                target = target.split("#")[0].strip()
                if not target or target.startswith(EXTERNAL_PREFIXES):
                    continue
                links += 1
                if not os.path.exists(os.path.normpath(os.path.join(dirpath, target))):
                    problems.append(
                        "%s -> %s (no such file in the repository)"
                        % (os.path.relpath(path, root), target)
                    )
    print("checked %d repository links across %d Markdown files" % (links, files))
    return problems


def main():
    build_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "_build"
    )
    if not os.path.isdir(build_dir):
        sys.exit("no such build directory: %s (run site/build.py first)" % build_dir)

    cache = {}
    problems = []
    pages = 0
    links = 0

    for dirpath, _dirnames, filenames in os.walk(build_dir):
        for filename in sorted(filenames):
            if not filename.endswith(".html"):
                continue
            pages += 1
            page = os.path.join(dirpath, filename)
            with open(page, encoding="utf-8") as fh:
                content = fh.read()
            page_ids = set(ID_RE.findall(content))
            rel_page = os.path.relpath(page, build_dir)

            for raw_href in LINK_RE.findall(content):
                href = html.unescape(raw_href)
                if not href or href.startswith(EXTERNAL_PREFIXES):
                    continue
                links += 1
                target, _, fragment = href.partition("#")
                if target:
                    resolved = os.path.normpath(os.path.join(dirpath, target))
                    if not os.path.isfile(resolved):
                        problems.append(
                            "%s -> %s (no such file in the built site)"
                            % (rel_page, href)
                        )
                        continue
                    target_ids = ids_in(resolved, cache)
                else:
                    target_ids = page_ids
                if fragment and fragment not in target_ids:
                    problems.append(
                        "%s -> %s (no element with that id)" % (rel_page, href)
                    )

    print("checked %d internal links across %d pages" % (links, pages))
    problems += check_sources(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    )
    if problems:
        print("\n%d broken link(s):" % len(problems))
        for problem in problems:
            print("  " + problem)
        return 1
    print("all internal links resolve.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
