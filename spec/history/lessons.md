# What the measurements got wrong

The format's constants are all measured, and the measurements are in
[`../../bench/results/`](../../bench/results/). This document is about the
times the measuring was wrong, about the time the testing was wrong, and
about the three trades that no single number can settle.

It exists because a benchmark that only ever confirms things is not being
read carefully enough. Every entry below changed a decision.

---

## 1. Counting instructions is not measuring speed

**What we did.** The machine these benchmarks run on is virtualised and its
run-to-run spread reaches 20 %, which is wider than most of the changes being
made. So throughput was replaced, for change-to-change comparisons, by
instruction counts under `callgrind`: deterministic, reproducible anywhere,
and immune to a noisy host. `bench/instructions/run.sh` was built for this
and the specification-version comparison in `RESULTS.md` still uses it.

**What went wrong.** The single largest encoder improvement in the project
executes *more* instructions than the code it replaced.

The block-mode lookahead was gating its Dynamic Passthrough test on one byte
read from a table. About a third of byte values are representable, so on
high-entropy input that test is a coin flip — an unpredictable branch,
resolved once per four bytes of every binary file. Widening the gate to
eight bytes turns away no position the one-byte test would have kept, and it
is right about 499 times in 500, so the branch predicts. On WebAssembly it
costs **3 % more instructions** and delivers **96 % more throughput**; on
random bytes, **195 %**.

Had the change been evaluated the way the project's own guidance said to
evaluate changes, it would have been read as a small regression and
discarded. It was found by accident, while measuring something else.

**What we should have known.** `bench/README.md` already said instruction
counts "ignore cache behaviour, branch prediction and memory bandwidth". The
warning was written and then not applied — the failure was not ignorance of
the limitation but treating a documented proxy as the measurement once it
was convenient. The partial version of this lesson had already been learned
once, in a commit titled *"measure in time not instructions"*, and it was
learned narrowly: the conclusion drawn was about that one change rather than
about the method.

**The rule now.** Instruction counts answer "is this the same amount of
work?" and nothing else. Any claim about speed needs a timing, and a timing
on a noisy host needs a method (§4). Where the two disagree, that
disagreement is a result and gets written down — `RESULTS.md` now flags the
one table where instruction counting reports the current encoder backwards.

## 2. Measure a proposal against the best alternative, not against today

**What we did.** A proposed `--binary` encoder mode was measured against the
shipped encoder, which is the obvious comparison and the one the proposal
asked for. It scored up to **+314 %** on binary.

**What went wrong.** Nothing in that number was false, and almost all of it
was irrelevant. Most of the gap was the mispredicted branch of §1 — a defect
in the baseline, not a virtue of the proposal. Fixing it, with byte-identical
output and no flag, took back two thirds to four fifths. The flag's actual
worth is **+31 % to +71 %**.

A proposal measured against a defect will always look good, and accepting it
would have frozen the defect in place: with `--binary` shipped, the slow path
would have been the one nobody was looking at.

**The rule now.** Before a feature is credited with a speedup, the baseline
gets one honest attempt at the same speedup without it. The
[`--binary` study](../../bench/results/binary-flag.md) reports both numbers
side by side for exactly this reason, and the harness prints them together
so the older one cannot be quoted alone.

## 3. Attribution needs more variants than the question has sides

**What we did.** "Does `--binary` help?" is a two-sided question, so the
first instinct was two encoders.

**What went wrong.** Two encoders cannot say *why*. The encoder has two
independent optional steps, and a `default` versus `--binary` comparison
confounds them with each other and with the gate of §1. Running six variants
— both steps, each step alone, neither, and two gate widths — produced three
findings the two-way comparison could not have:

- the cost of both optional steps on binary is entirely in *looking* for
  constructs that are not there, not in encoding them: with both removed the
  encoder reaches 2 808–2 898 MiB/s on every binary input regardless of shape;
- on ELF and WebAssembly, removing Fill is worth *more* than removing
  Dynamic Passthrough — so `--binary`, which removes DP and keeps Fill, was
  not even the right bundle for the files it named;
- the speedup was in the lookahead, not in the scan everyone assumed.

**The rule now.** When a measurement is meant to attribute rather than
compare, build the full grid over the independent axes, including the cell
nobody asked for. The cell nobody asked for is where §1 was found.

## 4. A threshold below the noise floor needs a method, not more runs

**What we did.** The throughput harness took the best of three rounds per
codec, each codec measured in turn.

**What went wrong.** A 4 % decision on a host with 20 % spread cannot be made
that way, and taking more rounds does not fix it: measuring the variants one
after another means any drift over the run lands entirely on whichever
variant was being measured when it happened.

**What works.** Two changes, both in `bench/speed/bench_binary_flag.c`:

- **Interleave.** Time every variant once, then all of them again. Drift is
  shared instead of attributed.
- **Pair.** Form the ratio to the baseline *within* each round, from timings
  taken milliseconds apart, and report the median of those ratios. A round
  the host was 10 % slow for makes both halves 10 % slow and cancels.

The range the rounds spanned is printed beside every figure, and no verdict
is claimed unless that whole range sits on one side of the threshold. This
turned differences that were previously unmeasurable here into ones that
reproduce.

**A related one.** A benchmark harness is code and can be wrong in ways that
quietly corrupt the numbers rather than crashing. This one had a heap bug
during development, found by running the harness itself under
AddressSanitizer, which is now a build target. Separately, a corpus glob once
pulled archive files into a run and interleaved error output into a results
table. Both argue the same thing: verify the harness, not only the subject.

## 5. A fixed set of test cases only ever covers what someone thought of

**What we did.** Four implementations are held byte-identical by a shared
vector set and by a generated differential corpus of 6,146 cases chosen for
the encoder's branch boundaries. Both were treated as settling the question.

**What went wrong.** The first differential *fuzzer*, pointed at C and Rust
in one process, found a disagreement in under a second, and a second one a
few minutes later. Neither could have been in either fixed set:

- **A lone trailing character outside Alphabet-N.** Section 10 makes it an
  `INVALID_CHARACTER`; Section 7.5 makes a one-character trailing group an
  `INVALID_FINAL_BLOCK`. Both conditions hold and the specification orders
  neither. C and Go reported the block, TypeScript reported the character,
  and Rust reported *both* — the character for a byte outside ASCII, the
  block for one inside it, because a conversion earlier in its pipeline
  rejected the first kind before the structural check ran. So it was never
  three implementations against one; it was three answers.
- **The Rust C ABI read its input as UTF-8.** Two consequences. It rejected
  a stray byte with `INVALID_CHARACTER` before the end-of-stream check that
  Section 7.3 explicitly orders ahead of it ("checked BEFORE reading"). And,
  worse, for *well-formed* multi-byte UTF-8 it counted one significant
  character where every other implementation counts two, three or four
  bytes — which moves every subsequent group boundary. The C header says the
  input is bytes; the format is defined over bytes; only that entry point
  disagreed.

Neither affected the decoding of any valid stream, so neither was a
vulnerability. Both were divergences between implementations of a frozen
format, which is the most serious kind of defect this repository can carry.

**Why the fixed sets could not have caught it.** The Rust vector runner
converted each vector's bytes with `from_utf8` and panicked otherwise, the
TypeScript one used a fatal UTF-8 decoder, the Python one called
`.decode("utf-8")`, and the shared consistency checker used
`errors="replace"` — which is lossy in exactly the way that matters, since
one U+FFFD stands in for a run of bytes and moves every group boundary after
it. So a vector that was not valid UTF-8 could not be *written* — every
vector in the set predating this is UTF-8-valid for that reason and not by
choice. The test set had been shaped by what the test harness could express,
and the harness had been shaped by the bug.

Adding the vectors made that concrete: four of the five language suites had
to be changed before the new cases could even run, and one of those changes
was not to a harness at all. The Python binding's `bytes` argument went
through `String::from_utf8` and reported an invalid character on failure —
the same defect as the C ABI, in a shipped library, on a path no vector
could reach.

**The rule now.** Every runner, and every entry point that takes bytes, maps
each byte to the character of the same value, which is what the format
means. The four
cases are pinned as `error_precedence` vectors. And the generated corpus is
no longer the last word: `c/fuzz/fuzz_differential.c` runs in CI, and its
job is the cases nobody thought of.

The general form is worth stating, because it is not about UTF-8. A test
harness that cannot express an input is indistinguishable from a test suite
that has decided the input does not matter — and it is silent about the
difference. When a fixed corpus is the evidence, ask what it *cannot*
contain.

---

# The three trades

These are not mistakes. They are the places where two things the format
wants are genuinely opposed, and where a single-objective optimisation would
have produced a worse format that scored better.

## Size against readability

Dynamic Passthrough exists so that text survives encoding as text: one
character per byte, and mostly the same characters. That is a property no
ratio measurement can see.

`MIN_FILL_IN_SEGMENT_BYTES` is where the two met. Eleven is the ratio
optimum. Sixteen is what shipped, and it gives up **1.0 % of ratio** to keep
370,000 more bytes of the corpus inside readable passthrough segments — and,
as it happened, to make the slowest decode in the benchmark a third faster.
Optimising ratio alone would have taken the eleven and lost both.

The alphabet is the same trade at a larger scale. `#`, `%`, `+`, `?` and `&`
are in Alphabet-N because they cost nothing in JSON, HTML and XML, which is
where encoded payloads actually travel. They are also exactly what a URL
encoder charges three characters for, which is why Base85N is 8 % *worse*
than Base64 in a query string — the one embedding it loses. A pure-size
choice across all embeddings picks a different alphabet and gives up the
three that matter to win the one that does not.

## Speed against features

The `--binary` study measured this exactly. Removing both optional steps
makes the encoder roughly two and a half times faster on binary than the
conforming one. It also makes it a different, larger, non-canonical encoding
— and gives up the property that makes Base85N smaller than every other
Base85 in the comparison.

The `--binary` proposal was the smaller version of the same offer: **+31 % to
+71 %**, in exchange for output that no conforming encoder would produce and
**+17.7 %** size on an uncompressed tar. It was declined. The full record is
in [`binary-flag-decision.md`](binary-flag-decision.md).

What is worth taking from it is which direction the answer came from. The
+195 % of §1 cost no feature, no output character and no API, because it was
not a trade at all — it was a defect. Trades should be refused until the
defects are exhausted, and the way to tell them apart is to try to have the
speed without giving anything up, and see what happens.

## Determinism against everything

There is exactly one encoding of any input. Section 6.5 makes every step
mandatory at every decision point the loop reaches, and Section 11.3 states
plainly that there is "no second canonical form and nothing to configure".

That is what four implementations agreeing byte for byte rests on, what the
shared test vectors can check, and what lets an encoder split its input
across cores and splice the results. It is also what rules out every
encoder option anybody will ever propose, including profitable ones — which
is the cost, and it is worth naming rather than discovering later.
