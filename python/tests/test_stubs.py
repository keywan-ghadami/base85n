# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The stubs describe the module that was actually built.

`base85n` is a compiled extension: a type checker learns nothing from it and
believes `base85n.pyi` instead. A stub that has drifted is therefore worse than
no stub at all -- it is a confident wrong answer, and nothing else in the suite
would notice, because the stub is not executed.

So it is read here as data, and checked against the module that is installed:
the same names, the same kinds of value, the same call signatures. The stub is
located next to the imported module rather than in the source tree, which makes
this a check on the wheel as well -- if the stub did not get packaged, this
fails.
"""

import ast
import inspect
from pathlib import Path

import pytest

import base85n


def _stub_path() -> Path:
    module_file = getattr(base85n, "__file__", None)
    assert module_file, "base85n has no __file__ to look beside"
    candidate = Path(module_file).with_name("__init__.pyi")
    assert candidate.is_file(), (
        f"no type stub next to the installed module ({candidate}). "
        "The wheel is supposed to carry one; check [tool.maturin] in pyproject.toml."
    )
    return candidate


STUB = ast.parse(_stub_path().read_text(encoding="utf-8"))


def _declared() -> dict:
    """Top-level names the stub declares, mapped to the node declaring them."""
    out = {}
    for node in STUB.body:
        if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
            out[node.target.id] = node
        elif isinstance(node, (ast.FunctionDef, ast.ClassDef)):
            out[node.name] = node
    return out


def _annotation_name(node: ast.expr) -> str:
    """The outermost name of an annotation: `Final[list[int]]` -> `list`."""
    if isinstance(node, ast.Subscript):
        head = node.value
        if isinstance(head, ast.Name) and head.id == "Final":
            return _annotation_name(node.slice)
        return _annotation_name(head)
    if isinstance(node, ast.Name):
        return node.id
    if isinstance(node, ast.Attribute):
        return node.attr
    raise AssertionError(f"unhandled annotation form: {ast.dump(node)}")


def test_the_stub_declares_exactly_what_the_module_exports():
    declared = set(_declared())
    exported = set(base85n.__all__) | {"__all__"}
    assert declared == exported, (
        f"only in the stub: {sorted(declared - exported)}; "
        f"only in the module: {sorted(exported - declared)}"
    )


def test_every_name_the_stub_declares_is_really_there():
    for name in _declared():
        assert hasattr(base85n, name), f"the stub declares {name}, the module has no such attribute"


@pytest.mark.parametrize(
    "name,node",
    [(n, d) for n, d in _declared().items() if isinstance(d, ast.AnnAssign)],
)
def test_annotated_values_have_the_annotated_type(name, node):
    expected = {"int": int, "str": str, "list": list, "bytes": bytes}[_annotation_name(node.annotation)]
    assert isinstance(getattr(base85n, name), expected)


def _stub_parameters(node: ast.FunctionDef):
    args = node.args
    positional = list(args.posonlyargs) + list(args.args)
    # Defaults are right-aligned against the positional parameters.
    padding = [inspect.Parameter.empty] * (len(positional) - len(args.defaults))
    defaults = padding + [ast.literal_eval(d) for d in args.defaults]
    kinds = [inspect.Parameter.POSITIONAL_ONLY] * len(args.posonlyargs)
    kinds += [inspect.Parameter.POSITIONAL_OR_KEYWORD] * len(args.args)
    return [
        (arg.arg, kind, default)
        for arg, kind, default in zip(positional, kinds, defaults)
    ]


@pytest.mark.parametrize("name", ["encode", "decode"])
def test_call_signatures_agree(name):
    node = _declared()[name]
    assert isinstance(node, ast.FunctionDef)
    live = [
        (p.name, p.kind, p.default)
        for p in inspect.signature(getattr(base85n, name)).parameters.values()
    ]
    assert _stub_parameters(node) == live


def test_the_exception_in_the_stub_is_the_exception_that_is_raised():
    node = _declared()["Base85NDecodeError"]
    assert isinstance(node, ast.ClassDef)
    assert [b.id for b in node.bases] == ["ValueError"]
    assert issubclass(base85n.Base85NDecodeError, ValueError)

    attributes = {
        stmt.target.id
        for stmt in node.body
        if isinstance(stmt, ast.AnnAssign) and isinstance(stmt.target, ast.Name)
    }
    with pytest.raises(base85n.Base85NDecodeError) as raised:
        base85n.decode("abcd|e")
    for attribute in attributes:
        assert hasattr(raised.value, attribute)
