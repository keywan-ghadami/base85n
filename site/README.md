# Project website

The site published at <https://keywan-ghadami.github.io/base85n/> is generated
from this directory by [`build.py`](build.py) and deployed by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml) on every push to
`main` that touches the site, the specification, `SECURITY.md`, or a
per-language README.

There is deliberately almost no content here. Every page except the landing page
is rendered from a Markdown file that already lives in the repository — the
specification, the security policy, the five implementation READMEs — so the
website cannot drift out of sync with the repository. Repository-relative links
in those files are rewritten either to the corresponding generated page or to an
absolute `github.com` URL.

## Layout

- `build.py` — the generator: page list, HTML template, link rewriting.
- `check_links.py` — verifies every internal link and `#anchor` in the built
  site resolves. CI fails the build if one does not.
- `assets/style.css` — the entire stylesheet. No framework, no external fonts,
  no JavaScript; the site works with light and dark colour schemes.
- `pages/index.md` — the only site-specific content. Its relative links are
  resolved against the repository root.
- `requirements.txt` — pinned build dependency (python-markdown).

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
