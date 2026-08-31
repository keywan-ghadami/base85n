# Project website

The site published at <https://keywan-ghadami.github.io/base85n/> is generated
from this directory by [`build.py`](build.py) and deployed by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) on every push to
`main` that touches the site, the specification, `SECURITY.md`, `IMPRESSUM.md`,
the benchmark report, or any README.

There is deliberately **no content here at all**. Every page, the landing page
included, is rendered from a Markdown file that already lives in the repository
— the project README, the specification, the security policy, the benchmark
report, the per-language READMEs, the Impressum — so the website cannot drift
out of sync with the repository. Repository-relative links in those files are
rewritten either to the corresponding generated page or to an absolute
`github.com` URL, and heading anchors are slugified the way GitHub does it, so a
link written for one works on the other.

The landing page used to be a hand-maintained copy of the README's opening under
`pages/index.md`. It said the same things in slightly different words, which is
exactly as long-lived as the next time only one of them is updated.

## Layout

- `build.py` — the generator: page list, HTML template, link rewriting.
- `check_links.py` — verifies every internal link and `#anchor` in the built
  site resolves, and every relative Markdown link in the repository against the
  working tree: a link to a file the site does not publish is rewritten to a
  `github.com` URL and is external by the time the built site is checked, so
  deleting that file would otherwise leave a 404 nothing catches. CI fails the
  build if one does not resolve.
- `assets/style.css` — the entire stylesheet. No framework, no external fonts,
  no JavaScript; the site works with light and dark colour schemes.
- `requirements.txt` — pinned build dependency (python-markdown).

`build.py` also holds `SOURCE_FILTERS`, which removes the parts of a source that
only make sense on GitHub: the README's badge row, and its link to this site.
Filters only ever *remove* — nothing on this site is written twice.

## Building locally

```sh
python3 -m pip install -r site/requirements.txt
python3 site/build.py --serve      # builds into site/_build, serves on :8000
python3 site/check_links.py        # optional: same check CI runs
```

`site/_build/` is git-ignored.

## Adding a page

Add a `Page(...)` entry to `PAGES` in `build.py`. If the new page should be
reachable from an existing Markdown link target, add that target to
`PATH_TO_PAGE` as well, so links to it are rewritten to the generated page
instead of falling through to a `github.com` URL.

## Where the specification pages come from

`spec/` holds exactly one document — the current specification — and
`spec/history/` holds every superseded one. That split is what a first-time
reader meets: one specification in the navigation, and a history area one link
away. The build enforces it, and fails if `spec/` ever holds more than one.

`build.py` discovers `base85n-v*.md` in both directories, reads the version,
status and date out of the metadata table at the top of each one, sorts them,
and generates a page per version. The current one keeps its own status, every
older one is labelled *superseded by* the version that followed it, and the
version in the footer of every page is the current one. A document and its
page therefore cannot disagree about what version it is.

What is not derived is either directory's index, because both are read on
GitHub as well and generating them would mean generating repository
documents. Instead the build *checks* them: a specification with no entry in
its directory's index, or an entry pointing at a file that is not there, fails
the build with the offending name. Links that leave a directory (`../`) are
not counted, so the history index can point at the current specification —
which it should. That check runs in CI, so forgetting is loud rather than
silent.

Retiring a version is therefore two moves: `git mv` the document into
`spec/history/`, and move its entry from `spec/README.md` to
`spec/history/README.md`. Nothing here changes.
