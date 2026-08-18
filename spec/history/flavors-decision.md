# Decision: no container flavors

**Status: not pursued, 2026-08-18.** Evaluated after 0.5.0 was declared final,
and left unbuilt — deliberately, and without closing the door.

A *flavor* would be an alternative 85-character alphabet, selected by a short
header at the start of the stream, for containers whose reserved characters are
not the ones Alphabet-N avoids. This is the analysis that decided against
building any, and the mechanics are recorded here so the decision can be revisited
without being re-derived.

## 1. The question, in one sentence

Flavors buy **passage, not size**. So the question is not "how much do we save",
it is:

> **Is there a concrete gate that rejects Alphabet-N, and that we have to get
> through?**

Without one, every flavor is work without a consumer. Where escaping is merely
*expensive*, the header barely pays for itself:

```
2-character header / escaping rate ≈ 2.35 %  ≈  85 characters  ≈  68 input bytes
```

Past 68 bytes a flavor is cheaper than escaping — but only where escaping is
allowed at all. Where it is allowed, the win is small. Where it is forbidden, the
flavor is the only way through. That asymmetry is the whole decision.

## 2. What a flavor is mechanically, and why that is the cost

**A flavor swaps characters, it cannot remove them.** The alphabet has to keep
exactly 85 characters or it is no longer a base-85 conversion. And the swap pool
is tiny: Alphabet-N already contains every printable ASCII character *except*
these ten, which are exactly the printable half of the R-Set:

```
space  "  '  ,  ;  \  |  <  >  &
```

A flavor can only draw from that pool, and must give something back for each
character it takes.

**The expensive part is not the alphabet, it is the donor pool.** Only **22** of
the 85 characters appear in any donor profile (Section 11.2, and re-derived from
the Section 4.2 table for this note) — and they are without exception the rare
punctuation, which is precisely what a flavor would want to trade away. Every
character swapped out invalidates all eight profiles that contain it.

The real damage comes after that: a character swapped *out* joins the R-Set, and
from then on needs a donor in **every** segment. If it is common in real text,
`k` rises almost always, profile viability gets stricter, and DP segments break
off earlier. That costs more than the profile table does.

Fixed cost per flavor:

| Item | Scope |
|---|---|
| Re-derive the profile table | the ~4.1 MB training and hold-out corpora of Section 14.2 |
| Re-fix the R-Set | must stay at exactly 13 entries — the mask is 13 bits |
| Conformance tests | all of Section 12, canonicity (12.3) included, per flavor |
| Implementation | two tables per flavor, in every encoder and decoder |

## 3. The candidates

### 3.1 Already safe without a flavor — nothing to do

These containers need nothing, because every character they reserve is already
outside Alphabet-N:

| Container | Reserves | In Alphabet-N? |
|---|---|---|
| SQL string `'…'` | `'`, and `\` in MySQL | none |
| Shell single-quoted word | `'` | none |
| CSV (RFC 4180, quoted and unquoted) | `,` `"` CRLF | none |
| JavaScript ordinary string `'…'` / `"…"` | `'` `"` `\` | none |
| JSON string | `"` `\` | none |
| XML/HTML text and quoted attribute | `<` `>` `&` `"` | none |

Four of the six candidates land here. **Value of a flavor: zero.**

CSV keeps two edge cases, but they are positional and no alphabet fixes them: a
leading `#` is a comment in some dialects, and a leading `=` `+` `-` `@` is
formula injection in a spreadsheet. The answer there is a prefix character or
quoting, not an alphabet.

### 3.2 JavaScript template literal — buildable, but barely worth it

| | |
|---|---|
| Problem | `` ` `` ends the literal, `${` starts an interpolation |
| Swap | out `` ` `` and `{`, in `<` and `>` |
| R-Set after | out `<` `>`, in `` ` `` `{` → still 13 ✓ |
| Profiles to re-derive | **6 of 8** (`` ` `` is in P0, P1, P2, P3, P4, P7; `{` in P4, P7) |
| DP damage | small — `` ` `` and `{` are rare in real text |

`$` does **not** have to go: with `{` out of the alphabet, `${` cannot occur.
That halves the intervention, because `$` sits in six profiles while `{` sits in
two, at ranks 11 and 12.

**The objection:** an ordinary JavaScript string literal is already safe. Anyone
who writes `'…'` instead of `` `…` `` needs nothing at all. The flavor solves a
problem that one line of calling code also solves.

**Verdict: no**, unless the template literal is imposed from outside — generated
code, or a framework that leaves no choice.

### 3.3 The Z85 alphabet — the only candidate with a real argument

| | |
|---|---|
| Purpose | output has to pass a validator that admits Z85 characters only |
| Swap | out `` ` `` `_` `~`, in `&` `<` `>` |
| R-Set after | out `&` `<` `>`, in `` ` `` `_` `~` → still 13 ✓ |
| Profiles to re-derive | **all 8** — `~` appears in every profile |
| DP damage | **substantial** — `_` becomes an R-Set character |

The benefit is concrete and unreachable any other way: through a Z85 whitelist
gate, Base85N content costs **~1.007** against Z85's **1.250** — about 19 % fewer
characters, with the gate none the wiser.

The header lands well here too: `&` `<` `>` are exactly the three characters Z85
has and Alphabet-N lacks, so a header like `<Z` is itself two Z85 characters and
the *entire* stream, header included, passes the gate.

The price is real. `_` is extremely common in source code — snake_case, private
members — so as an R-Set character it sets a mask bit in nearly every code
segment, `k` rises, and segments break off earlier. The flavor would be good on
binary and prose, and noticeably weaker than Alphabet-N on source code.

**Verdict: only with a named consumer.** This is not a feature to build on spec.

### 3.4 The Ascii85 alphabet — constructible, but it destroys DP

| | |
|---|---|
| Swap | out `v w x y z { } ~`, in `" ' , ; \ < > &` |
| R-Set after | `v w x y z { } ~ \|` plus whitespace → exactly 13 ✓ |
| Profiles to re-derive | all 8 |
| DP damage | **prohibitive** |

Ascii85 is ASCII 33–117, so it does not contain `v`, `w`, `x`, `y`, `z` — five
ordinary lowercase letters, which would have to join the R-Set. Every English
text segment then sets four or five mask bits before a single punctuation mark
appears; `k` starts at 5 instead of 0, profile viability collapses, DP segments
get very short.

The result is a ratio near 1.25 — where Ascii85 already is. The flavor costs
exactly the two axes Base85N wins on, ratio on text and readability, and returns
nothing for them.

**Verdict: no.** Anyone who must pass an Ascii85 gate should use Ascii85.

### 3.5 Structurally impossible — not worth examining

| Candidate | Reason |
|---|---|
| URL / query string | RFC 3986 unreserved is **66 characters**; even with every sub-delim it reaches 77. A base-85 encoding needs 85. There is no URL-safe Base85, from anyone, and there cannot be one. |
| Unquoted YAML scalar | The rules are positional — a leading `#` `-` `%` `@` `` ` ``, or the sequence `: `. No alphabet swap fixes that, because every alphabet contains *something* that can land first. Quote the scalar and it becomes the JSON case, which is already safe. |
| Unquoted HTML attribute | Same pattern, same answer: quote the attribute. |

## 4. Keeping the option open costs nothing

The decisive point for the trade-off: **this does not have to be decided now, and
holding it open costs not one line.**

Because the first header character would lie *outside* Alphabet-N, a v0.5.0
decoder already rejects it today with `INVALID_CHARACTER` at position 0 — with
nothing specified, and nothing to add. It consumes **zero** signal values:
`FUTURE_SIGNAL_SPACE` stays intact at all 3,149,509 values for other purposes.

So the order is free to choose: consumer first, then flavor. Not the other way
round.

## 5. The header scheme, if it is ever built

Only the **first** character has to lie outside Alphabet-N; that alone announces
a header. The second may be an ordinary alphabet character, which keeps the
header readable.

```
character 1:  container family  (9 options: " ' , ; \ | < > &)
character 2:  flavor identifier (94 options)   →  846 combinations
```

| Header | Flavor |
|---|---|
| `<Z` | Z85 — both characters are Z85, so the header passes the gate too |
| `;J` | JS template — `;` is inert in JS, JSON, XML, SQL and shell |
| `"` `'` `,` `\` `\|` `>` `&` | seven families held for later |
| no header | Alphabet-N, unchanged |

If that ever runs out, a third character can be added without touching existing
headers.

Two points would be normative:

- **Start of stream only.** A switch mid-stream would make the alphabet state
  that carries across signal boundaries, which breaks Section 11.3: a worker
  starting at byte 4,000,000 would not know which alphabet it is reading. At the
  start it is harmless — every worker reads two characters, then begins.
- **Canonicity is defined per flavor.** Same input, different flavor, different
  output. Unlike the rejected chunk-size parameter, though, the flavor is
  *declared in the stream* and recoverable from the output — not a hidden knob.
  Section 12.3 would then run per flavor, multiplying the test matrix.

## 6. Summary

| Candidate | Benefit | Cost | Verdict |
|---|---|---|---|
| SQL string | none — already safe | – | **moot** |
| Shell single-quoted | none — already safe | – | **moot** |
| CSV unquoted | none — already safe | – | **moot** |
| JS template literal | small — an ordinary string does it | 6 profiles re-derived | **no** |
| Ascii85 alphabet | passes an Ascii85 gate | all profiles, DP destroyed | **no** |
| **Z85 alphabet** | **19 % through a whitelist gate** | all profiles, weaker DP on code | **only with a consumer** |
| URL / YAML / HTML attribute | – | – | **impossible** |

**Of six candidates one survives, and it is waiting for a use case.**

That is not a negative result. It is evidence for the alphabet choice: Alphabet-N
was picked from the start to fit through the usual containers, and almost every
flavor candidate being unnecessary is exactly what
[`RESULTS.md`](../../bench/results/RESULTS.md) means when it says the alphabet is
worth more than the ratio.

**Decision: do not take this path now.** Keep the mechanics on file, aim any
future version at other content, and build the Z85 flavor when somebody concretely
asks for it. The option does not expire.

## Note on the numbers

The profile counts above were re-derived from the Section 4.2 table rather than
carried over from the proposal, which had two of them one too low: `` ` `` appears
in six profiles (P0, P1, P2, P3, P4, P7), not five — P3 carries it at rank 10 —
and `$` likewise appears in six, not five. Neither changes a verdict: the JS
template flavor costs six profiles instead of five, and the Z85 flavor already
cost all eight through `~`.
