# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Base85N: a compact encoding for data embedded in text formats.

Everything in this package comes from `base85n.base85n`, the extension module
built from Rust -- see `src/lib.rs`. The package exists so that the type stubs
and the PEP 561 marker have somewhere to live that a type checker recognises;
it adds no behaviour of its own, and re-exports exactly what the extension
lists in its `__all__`.
"""

from . import base85n as _extension
from .base85n import *  # noqa: F401,F403

# The extension's own docstring is the one worth showing, and its `__all__` is
# the single list of what this package exports -- kept there, next to the code
# that defines the names, rather than transcribed here.
__doc__ = _extension.__doc__
__all__ = list(_extension.__all__)
