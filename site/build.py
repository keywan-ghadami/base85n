#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Static site generator for the Base85N project website.

The site has no content of its own: every page, the landing page included, is
rendered from a Markdown file the repository already ships -- the README, the
specification, the security policy, the benchmark report, the per-language
documentation -- so the website cannot drift from the repository. Repository-
relative links in those files are rewritten either to the corresponding
generated page or to an absolute github.com URL.

Where a source carries something that only makes sense on GitHub -- the badge
row at the top of the README, its link *to* this site -- a filter in
``SOURCE_FILTERS`` removes it. Filters only ever remove; nothing on this site
is written twice.

Usage::

    pip install -r site/requirements.txt
    python3 site/build.py [--output DIR] [--serve]

The default output directory is ``site/_build``, which is git-ignored.
"""

from __future__ import annotations

import argparse
import html
import os
import re
import shutil
import sys

try:
    import markdown
except ImportError:  # pragma: no cover - developer convenience
    sys.exit(
        "python-markdown is required: pip install -r site/requirements.txt"
    )

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SITE_DIR = os.path.join(REPO_ROOT, "site")

GITHUB_REPO = "https://github.com/keywan-ghadami/base85n"
GITHUB_BLOB = GITHUB_REPO + "/blob/main/"
GITHUB_TREE = GITHUB_REPO + "/tree/main/"

SITE_TITLE = "Base85N"
SITE_TAGLINE = (
    "A binary-to-text encoding that is denser than Base64 - and, for "
    "text-like input, stays readable."
)

SPEC_FILE_RE = re.compile(r"^base85n-v(\d+)\.(\d+)\.(\d+)\.md$")
# The metadata table at the top of a specification. Older versions bold the
# field name, newer ones do not, so the asterisks are optional here.
SPEC_FIELD_RE = re.compile(
    r"^\|\s*\*{0,2}(Version|Status|Date)\*{0,2}\s*\|\s*([^|]+?)\s*\|", re.M
)


def discover_specs():
    """Every published specification, newest first.

    The specification directory is the source of truth: a new version is a new
    file, and the file carries its own version, status and date in the metadata
    table at the top. Deriving the page list from that means adding a version
    is one commit to `spec/` and nothing here -- and it means the site cannot
    disagree with the document about what version it is.

    Returns a list of dicts with `path`, `version` (a sortable tuple), `label`,
    `status` and `date`.
    """
    spec_dir = os.path.join(REPO_ROOT, "spec")
    found = []
    for name in os.listdir(spec_dir):
        m = SPEC_FILE_RE.match(name)
        if not m:
            continue
        with open(os.path.join(spec_dir, name), encoding="utf-8") as fh:
            head = fh.read(2048)
        fields = dict(SPEC_FIELD_RE.findall(head))
        label = fields.get("Version", ".".join(m.groups()))
        if label != ".".join(m.groups()):
            raise SystemExit(
                "spec/%s says it is version %s; the file name says %s"
                % (name, label, ".".join(m.groups()))
            )
        found.append({
            "path": "spec/" + name,
            "version": tuple(int(g) for g in m.groups()),
            "label": label,
            "status": fields.get("Status", "Draft"),
            "date": fields.get("Date", ""),
        })
    if not found:
        raise SystemExit("no specification documents found in spec/")
    found.sort(key=lambda s: s["version"], reverse=True)
    return found


def check_spec_index(specs):
    """Every discovered specification is listed in `spec/README.md`, and
    nothing is listed that does not exist.

    The index is a repository document -- it is read on GitHub too -- so it is
    written by hand rather than generated. This is what stops a new version
    from being published as a page nobody links to, or a link from outliving
    its file. The check runs at build time, which is CI.
    """
    index_path = os.path.join(REPO_ROOT, "spec", "README.md")
    with open(index_path, encoding="utf-8") as fh:
        index = fh.read()
    missing = [s["path"] for s in specs if os.path.basename(s["path"]) not in index]
    linked = set(re.findall(r"base85n-v\d+\.\d+\.\d+\.md", index))
    stale = sorted(linked - {os.path.basename(s["path"]) for s in specs})
    if missing or stale:
        raise SystemExit(
            "spec/README.md is out of step with spec/:\n"
            + "".join("  not listed: %s\n" % p for p in missing)
            + "".join("  listed but missing: spec/%s\n" % p for p in stale)
        )


SPECS = discover_specs()
check_spec_index(SPECS)

# Shown in the footer. Derived, so a new specification version does not leave a
# stale number on every page.
SPEC_VERSION = SPECS[0]["label"]


def spec_pages():
    """One page per specification document, newest first.

    The newest carries its own status; every older one is described as
    superseded by the version that followed it, which is the next entry up.
    """
    pages = []
    for i, spec in enumerate(SPECS):
        if i == 0:
            state = spec["status"]
        else:
            state = "Superseded by " + SPECS[i - 1]["label"]
        subtitle = " - ".join(
            part for part in ("Version " + spec["label"], state, spec["date"]) if part
        )
        pages.append(Page(
            source=spec["path"],
            output=spec["path"].replace(".md", ".html"),
            title="Base85N Specification v" + spec["label"],
            toc=True,
            subtitle=subtitle,
            strip_first_heading=True,
        ))
    return pages

MARKDOWN_EXTENSIONS = ["tables", "fenced_code", "toc", "attr_list", "sane_lists"]

# Anything that is not a letter, digit, underscore, hyphen or space. GitHub
# drops these from a heading before turning it into an anchor.
ANCHOR_DROP_RE = re.compile(r"[^\w\- ]", re.UNICODE)


def github_slugify(value, separator):
    """Heading -> anchor id, the way GitHub does it.

    Every Markdown file here is read on GitHub as well as on this site, and its
    cross-references are written against GitHub's ids. python-markdown's own
    slugify collapses runs of whitespace, so "AI-generated code - notice"
    becomes ``ai-generated-code-notice`` there and ``ai-generated-code--notice``
    on GitHub -- one dash apart, and a broken link on whichever side is not
    matched. Not collapsing is the whole difference.
    """
    return ANCHOR_DROP_RE.sub("", value.lower()).replace(" ", separator)


class Page:
    """One generated HTML page, rendered from one Markdown source file."""

    def __init__(self, source, output, title, nav_label=None, toc=False,
                 subtitle=None, strip_first_heading=False, link_base=None,
                 body_class=""):
        self.source = source  # repo-relative path of the Markdown source
        self.output = output  # site-relative path of the generated HTML
        self.title = title
        self.nav_label = nav_label
        self.toc = toc
        self.subtitle = subtitle
        self.strip_first_heading = strip_first_heading
        # Directory that relative links in this source resolve against: the
        # source's own directory, so the same links keep working on GitHub.
        self.link_base = (
            os.path.dirname(source) if link_base is None else link_base
        )
        # Extra class on the page wrapper, for the few rules that apply to one
        # page only (see ``.page-home`` in assets/style.css).
        self.body_class = body_class


PAGES = [
    Page(
        # The landing page is the README, so the two cannot disagree about what
        # Base85N is or what it measures.
        source="README.md",
        output="index.html",
        title="Base85N",
        nav_label="Home",
        subtitle=SITE_TAGLINE,
        strip_first_heading=True,
        body_class=" page-home",
    ),
    Page(
        source="spec/README.md",
        output="spec/index.html",
        title="Specification versions",
        nav_label="Spec",
        subtitle="Every published version of the Base85N specification.",
        strip_first_heading=True,
    ),
    *spec_pages(),
    Page(
        source="bench/results/RESULTS.md",
        output="benchmarks/index.html",
        title="Benchmark results",
        nav_label="Benchmarks",
        toc=True,
        subtitle=(
            "Base85N against Ascii85, Z85 and RFC 1924 Base85, on size and "
            "throughput - including where they win."
        ),
        strip_first_heading=True,
    ),
    Page(
        source="bench/README.md",
        output="benchmarks/method.html",
        title="Benchmark method",
        subtitle="What is measured, what the corpus is, and how to reproduce it.",
        strip_first_heading=True,
    ),
    Page(
        source="SECURITY.md",
        output="security.html",
        title="Security",
        nav_label="Security",
        toc=True,
        subtitle=(
            "Threat model, reporting contact, what has been done, what has "
            "not, and what you should do."
        ),
        strip_first_heading=True,
    ),
    Page(
        source="rust/README.md",
        output="implementations/rust.html",
        title="Rust implementation",
        strip_first_heading=True,
    ),
    Page(
        source="go/README.md",
        output="implementations/go.html",
        title="Go implementation",
        strip_first_heading=True,
    ),
    Page(
        source="typescript/README.md",
        output="implementations/typescript.html",
        title="TypeScript implementation",
        strip_first_heading=True,
    ),
    Page(
        source="c/README.md",
        output="implementations/c.html",
        title="C implementation",
        strip_first_heading=True,
    ),
    Page(
        source="python/README.md",
        output="implementations/python.html",
        title="Python bindings",
        strip_first_heading=True,
    ),
]

# Repository paths that have a generated page. Keys are repo-relative paths
# exactly as they may appear in a Markdown link target.
PATH_TO_PAGE = {
    "README.md": "index.html",
    "spec/README.md": "spec/index.html",
    **{s["path"]: s["path"].replace(".md", ".html") for s in SPECS},
    "SECURITY.md": "security.html",
    "bench/results/RESULTS.md": "benchmarks/index.html",
    "bench/README.md": "benchmarks/method.html",
    "bench": "benchmarks/index.html",
    "rust/README.md": "implementations/rust.html",
    "go/README.md": "implementations/go.html",
    "typescript/README.md": "implementations/typescript.html",
    "c/README.md": "implementations/c.html",
    "python/README.md": "implementations/python.html",
    # Directory links point at the language's page rather than at a listing.
    "rust": "implementations/rust.html",
    "go": "implementations/go.html",
    "typescript": "implementations/typescript.html",
    "c": "implementations/c.html",
    "python": "implementations/python.html",
}

# A line that is nothing but shields.io badges, and the README bullet that
# links to this very site. Both are GitHub chrome: the badges report CI state
# to someone reading the repository, and the site does not need a link to
# itself in its own first list.
BADGE_LINE_RE = re.compile(r"^\s*(?:\[!\[[^\]]*\]\([^)]*\)\]\([^)]*\)\s*)+$", re.MULTILINE)
SELF_LINK_RE = re.compile(r"^- \U0001F310 \*\*\[Project website\][^\n]*\n", re.MULTILINE)


def strip_github_chrome(text):
    """Remove what only makes sense when the README is read on GitHub."""
    text = BADGE_LINE_RE.sub("", text, count=1)
    return SELF_LINK_RE.sub("", text)


# Per-source Markdown filters, applied before conversion. Keyed by the same
# repo-relative path a Page names.
SOURCE_FILTERS = {
    "README.md": strip_github_chrome,
}


TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="{description}">
<link rel="stylesheet" href="{root}assets/style.css">
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>85</text></svg>">
</head>
<body>
<a class="skip-link" href="#content">Skip to content</a>
<header class="site-header">
  <div class="wrap header-inner">
    <a class="brand" href="{root}index.html"><span class="brand-mark">85</span><span>Base85N</span></a>
    <nav class="site-nav">{nav}
      <a class="nav-external" href="{repo}">GitHub</a>
    </nav>
  </div>
</header>
<div class="wrap page{page_class}">
{sidebar}
<main id="content" class="content">
<div class="page-head">
<h1>{heading}</h1>
{subtitle}
</div>
{body}
</main>
</div>
<footer class="site-footer">
  <div class="wrap">
    <p><strong>Base85N</strong> - specification v{spec_version} (draft), four
    implementations and a set of Python bindings.</p>
    <p class="footer-warn">Specification and implementations were produced with
    substantial AI assistance and have not been independently audited. Read the
    <a href="{root}security.html">security policy</a> before decoding untrusted
    input.</p>
    <p class="footer-meta">Source: <a href="{repo}">github.com/keywan-ghadami/base85n</a>
    &middot; This page is generated from <a href="{source_url}">{source}</a>
    &middot; Contact: <a href="mailto:keywan.ghadami@gmail.com">keywan.ghadami@gmail.com</a></p>
  </div>
</footer>
</body>
</html>
"""


def relative_url(from_output, to_output):
    """URL of ``to_output`` as seen from the page at ``from_output``."""
    from_dir = os.path.dirname(from_output) or "."
    rel = os.path.relpath(to_output, from_dir)
    return rel.replace(os.sep, "/")


def rewrite_link(target, link_base, output_path):
    """Rewrite one Markdown link target for the generated site."""
    if not target or target.startswith(("http://", "https://", "mailto:", "#")):
        return target

    anchor = ""
    if "#" in target:
        target, anchor = target.split("#", 1)
        anchor = "#" + anchor
    if not target:
        return anchor

    resolved = os.path.normpath(os.path.join(link_base, target))
    resolved = resolved.replace(os.sep, "/").rstrip("/")

    if resolved in PATH_TO_PAGE:
        return relative_url(output_path, PATH_TO_PAGE[resolved]) + anchor

    # Everything else stays in the repository: link to GitHub so the page is
    # useful rather than 404-ing on a path the site does not publish.
    on_disk = os.path.join(REPO_ROOT, resolved)
    prefix = GITHUB_TREE if os.path.isdir(on_disk) else GITHUB_BLOB
    return prefix + resolved + anchor


HREF_RE = re.compile(r'(<a\b[^>]*?\shref=")([^"]*)(")', re.IGNORECASE)


def rewrite_links(body_html, link_base, output_path):
    def replace(match):
        target = html.unescape(match.group(2))
        return match.group(1) + html.escape(
            rewrite_link(target, link_base, output_path), quote=True
        ) + match.group(3)

    return HREF_RE.sub(replace, body_html)


FIRST_H1_RE = re.compile(r"^\s*<h1[^>]*>.*?</h1>\s*", re.IGNORECASE | re.DOTALL)


def build_nav(output_path):
    items = []
    for page in PAGES:
        if not page.nav_label:
            continue
        href = relative_url(output_path, page.output)
        current = ' class="current"' if page.output == output_path else ""
        items.append(
            '\n      <a href="%s"%s>%s</a>' % (href, current, page.nav_label)
        )
    return "".join(items)


def first_paragraph_text(body_html):
    match = re.search(r"<p>(.*?)</p>", body_html, re.DOTALL)
    if not match:
        return SITE_TAGLINE
    text = re.sub(r"<[^>]+>", "", match.group(1))
    text = html.unescape(text).strip().replace("\n", " ")
    return (text[:180] + "...") if len(text) > 180 else text


def render_page(page, output_dir):
    source_abs = os.path.join(REPO_ROOT, page.source)
    with open(source_abs, encoding="utf-8") as fh:
        text = fh.read()

    source_filter = SOURCE_FILTERS.get(page.source)
    if source_filter:
        text = source_filter(text)

    converter = markdown.Markdown(
        extensions=MARKDOWN_EXTENSIONS,
        extension_configs={
            "toc": {
                "permalink": "#",
                "toc_depth": "2-3",
                "slugify": github_slugify,
            }
        },
    )
    body = converter.convert(text)
    toc_html = getattr(converter, "toc", "")

    if page.strip_first_heading:
        body = FIRST_H1_RE.sub("", body, count=1)

    body = rewrite_links(body, page.link_base, page.output)
    toc_html = rewrite_links(toc_html, page.link_base, page.output)

    depth = page.output.count("/")
    root = "../" * depth

    sidebar = ""
    if page.toc and toc_html:
        sidebar = (
            '<aside class="toc" aria-label="Table of contents">'
            '<p class="toc-title">On this page</p>%s</aside>' % toc_html
        )

    rendered = TEMPLATE.format(
        title=html.escape(
            page.title if page.output == "index.html"
            else "%s - %s" % (page.title, SITE_TITLE)
        ),
        description=html.escape(page.subtitle or first_paragraph_text(body)),
        heading=html.escape(page.title),
        subtitle=(
            '<p class="subtitle">%s</p>' % html.escape(page.subtitle)
            if page.subtitle else ""
        ),
        body=body,
        nav=build_nav(page.output),
        sidebar=sidebar,
        page_class=(" has-toc" if sidebar else "") + page.body_class,
        root=root,
        repo=GITHUB_REPO,
        source=html.escape(page.source),
        source_url=GITHUB_BLOB + page.source,
        spec_version=SPEC_VERSION,
    )

    destination = os.path.join(output_dir, page.output)
    os.makedirs(os.path.dirname(destination) or output_dir, exist_ok=True)
    with open(destination, "w", encoding="utf-8") as fh:
        fh.write(rendered)
    return destination


def build(output_dir):
    if os.path.exists(output_dir):
        shutil.rmtree(output_dir)
    os.makedirs(output_dir)

    shutil.copytree(
        os.path.join(SITE_DIR, "assets"), os.path.join(output_dir, "assets")
    )
    # GitHub Pages must serve these files verbatim, not run them through Jekyll.
    open(os.path.join(output_dir, ".nojekyll"), "w").close()

    for page in PAGES:
        print("  %-40s <- %s" % (page.output, page.source))
        render_page(page, output_dir)

    print("built %d pages into %s" % (len(PAGES), output_dir))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        default=os.path.join(SITE_DIR, "_build"),
        help="output directory (default: site/_build)",
    )
    parser.add_argument(
        "--serve",
        action="store_true",
        help="serve the built site on http://localhost:8000 afterwards",
    )
    args = parser.parse_args()

    build(args.output)

    if args.serve:
        import functools
        import http.server

        handler = functools.partial(
            http.server.SimpleHTTPRequestHandler, directory=args.output
        )
        print("serving on http://localhost:8000 (Ctrl-C to stop)")
        http.server.ThreadingHTTPServer(("", 8000), handler).serve_forever()


if __name__ == "__main__":
    main()
