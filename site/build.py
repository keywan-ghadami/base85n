#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Static site generator for the Base85N project website.

The site has no content of its own beyond ``site/pages/``: the specification,
the security policy and the per-language documentation are rendered from the
same Markdown files the repository ships, so the website cannot drift from the
repository. Repository-relative links in those files are rewritten either to
the corresponding generated page or to an absolute github.com URL.

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

MARKDOWN_EXTENSIONS = ["tables", "fenced_code", "toc", "attr_list", "sane_lists"]


class Page:
    """One generated HTML page, rendered from one Markdown source file."""

    def __init__(self, source, output, title, nav_label=None, toc=False,
                 subtitle=None, strip_first_heading=False, link_base=None):
        self.source = source  # repo-relative path of the Markdown source
        self.output = output  # site-relative path of the generated HTML
        self.title = title
        self.nav_label = nav_label
        self.toc = toc
        self.subtitle = subtitle
        self.strip_first_heading = strip_first_heading
        # Directory that relative links in this source resolve against. It is
        # the source's own directory for files that live in the repository
        # proper (so their links keep working on GitHub too), but the repository
        # root for site-only pages under site/pages/, which have no meaningful
        # location of their own.
        self.link_base = (
            os.path.dirname(source) if link_base is None else link_base
        )


PAGES = [
    Page(
        source="site/pages/index.md",
        output="index.html",
        title="Base85N",
        nav_label="Home",
        subtitle=SITE_TAGLINE,
        strip_first_heading=True,
        link_base="",
    ),
    Page(
        source="spec/README.md",
        output="spec/index.html",
        title="Specification versions",
        nav_label="Spec",
        subtitle="Every published version of the Base85N specification.",
        strip_first_heading=True,
    ),
    Page(
        source="spec/base85n-v0.2.0.md",
        output="spec/base85n-v0.2.0.html",
        title="Base85N Specification v0.2.0",
        toc=True,
        subtitle="Version 0.2.0 - Draft - 2026-08-10",
        strip_first_heading=True,
    ),
    Page(
        source="spec/base85n-v0.1.0.md",
        output="spec/base85n-v0.1.0.html",
        title="Base85N Specification v0.1.0",
        toc=True,
        subtitle="Version 0.1.0 - Superseded by 0.2.0 - 2026-08-10",
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
        title="Python implementation",
        strip_first_heading=True,
    ),
]

# Repository paths that have a generated page. Keys are repo-relative paths
# exactly as they may appear in a Markdown link target.
PATH_TO_PAGE = {
    "README.md": "index.html",
    "spec/README.md": "spec/index.html",
    "spec/base85n-v0.2.0.md": "spec/base85n-v0.2.0.html",
    "spec/base85n-v0.1.0.md": "spec/base85n-v0.1.0.html",
    "SECURITY.md": "security.html",
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
    <p><strong>Base85N</strong> - specification v0.2.0 (draft) and five reference
    implementations.</p>
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

    converter = markdown.Markdown(
        extensions=MARKDOWN_EXTENSIONS,
        extension_configs={"toc": {"permalink": "#", "toc_depth": "2-3"}},
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
        page_class=" has-toc" if sidebar else "",
        root=root,
        repo=GITHUB_REPO,
        source=html.escape(page.source),
        source_url=GITHUB_BLOB + page.source,
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
