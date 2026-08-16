# Project website

The site published at <https://keywan-ghadami.github.io/base85n/> is generated
from this directory by [`build.py`](build.py) and deployed by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) on every push to
`main` that touches the site, the specification, `SECURITY.md`, the benchmark
report, or any README.

There is deliberately **no content here at all**. Every page, the landing page
included, is rendered from a Markdown file that already lives in the repository
— the project README, the specification, the security policy, the benchmark
report, the per-language READMEs — so the website cannot drift out of sync with
the repository. Repository-relative links in those files are rewritten either to
the corresponding generated page or to an absolute `github.com` URL, and heading
anchors are slugified the way GitHub does it, so a link written for one works on
the other.

The landing page used to be a hand-maintained copy of the README's opening under
`pages/index.md`. It said the same things in slightly different words, which is
exactly as long-lived as the next time only one of them is updated.

## Layout

- `build.py` — the generator: page list, HTML template, link rewriting.
- `check_links.py` — verifies every internal link and `#anchor` in the built
  site resolves. CI fails the build if one does not.
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

## Adding a specification version

Nothing here. Drop `spec/base85n-vX.Y.Z.md` in and add its row to
`spec/README.md`; the site picks it up on the next build.

`build.py` discovers every `spec/base85n-v*.md`, reads the version, status and
date out of the metadata table at the top of each one, sorts them, and
generates a page per version. The newest keeps its own status, every older one
is labelled *superseded by* the version that followed it, and the version in
the footer of every page is the newest one found. A document and its page
therefore cannot disagree about what version it is, and no page is left
labelled current after a successor lands.

The one thing that is not derived is `spec/README.md`'s table, because that
file is read on GitHub as well and generating it would mean generating a
repository document. Instead the build *checks* it: a specification with no row
in the index, or a row pointing at a file that is not there, fails the build
with the offending name. That check runs in CI, so forgetting is loud rather
than silent.
