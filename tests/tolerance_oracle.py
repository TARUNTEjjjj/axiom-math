#!/usr/bin/env python3
"""Check the approximate answer-checking policy against Python Fraction.

The policy is specified in docs/answer-checking.md. This oracle restates it with
Python's arbitrary-precision fractions, which share no code with the C++ 192-bit
comparison. It first recomputes every worked example in that document (so the
published verdicts are checked even without a build), then, given the CLI, runs
those examples, seeded random and near-boundary cases, full-width extremes,
invalid tolerances and CLI contracts. Inputs are synthetic.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import os
from pathlib import Path
import random
import subprocess

MINIMUM = -(2**63)
MAXIMUM = 2**63 - 1
SPEC = Path(__file__).resolve().parent.parent / "docs" / "answer-checking.md"


def verdict(expected: Fraction, answer: Fraction, absolute: Fraction, relative: Fraction) -> str:
    """The policy, written directly from its definition."""
    if absolute < 0 or relative < 0 or relative >= 1:
        return "invalid"
    distance = abs(answer - expected)
    return "accept" if distance <= absolute or distance <= relative * abs(expected) else "reject"


def representable(value: Fraction) -> bool:
    return MINIMUM <= value.numerator <= MAXIMUM and value.denominator <= MAXIMUM


def text(value: Fraction) -> str:
    return str(value.numerator) if value.denominator == 1 else f"{value.numerator}/{value.denominator}"


def examples() -> list[tuple[str, str, str, str, str]]:
    lines = SPEC.read_text(encoding="utf-8").splitlines()
    start = lines.index("<!-- tolerance-examples:start -->")
    end = lines.index("<!-- tolerance-examples:end -->")
    rows = []
    for line in lines[start + 3:end]:
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        if len(cells) != 6:
            raise SystemExit(f"Malformed worked-example row: {line!r}")
        rows.append(tuple(cells[:5]))
    if len(rows) < 20:
        raise SystemExit(f"Expected the full worked-example table, found {len(rows)} rows")
    return rows


def check_examples(rows: list[tuple[str, str, str, str, str]]) -> None:
    for row in rows:
        expected, answer, absolute, relative, published = row
        values = [Fraction(cell) for cell in (expected, answer, absolute, relative)]
        if not all(representable(value) for value in values):
            raise SystemExit(f"Worked example uses an unrepresentable value: {row}")
        computed = verdict(*values)
        if computed != published:
            raise SystemExit(f"Worked example {row} is published as {published}, but Fraction gives {computed}")
    # The table claims that a floating-point check would reject 1.1 for 1 within
    # 0.1. Confirm that claim rather than assert it from memory.
    if not abs(1.1 - 1.0) > 0.1:
        raise SystemExit("The documented floating-point counterexample no longer holds")
    if verdict(Fraction(1), Fraction("1.1"), Fraction("0.1"), Fraction(0)) != "accept":
        raise SystemExit("The exact policy must accept the floating-point counterexample")
    # Zero, negatives and the relative bound's sign property, on a small grid.
    grid = [Fraction(n, d) for n in range(-6, 7) for d in (1, 2, 3)]
    for expected in grid:
        for answer in grid:
            if verdict(expected, answer, Fraction(0), Fraction(0)) != ("accept" if expected == answer else "reject"):
                raise SystemExit(f"Zero tolerances must be exact equality: {expected}, {answer}")
            for absolute, relative in [(Fraction(1, 2), Fraction(0)), (Fraction(0), Fraction(9, 10)), (Fraction(1, 3), Fraction(1, 4))]:
                if verdict(expected, answer, absolute, relative) != verdict(-expected, -answer, absolute, relative):
                    raise SystemExit(f"Negating both values changed the decision: {expected}, {answer}")
            # A relative bound alone accepts only 0 for 0, and otherwise only a
            # nonzero answer with the expected value's sign.
            if verdict(expected, answer, Fraction(0), Fraction(99, 100)) == "accept":
                same_sign = (answer > 0) == (expected > 0) and answer != 0
                if (answer != 0) if expected == 0 else not same_sign:
                    raise SystemExit(f"A relative bound accepted {answer} for {expected}")


def run(binary: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(binary), *arguments], text=True, capture_output=True, timeout=5, check=False)


def expect_cli(binary: Path, values: tuple[Fraction, Fraction, Fraction, Fraction],
               arguments: tuple[str, str, str, str] | None = None) -> str:
    wanted = verdict(*values)
    arguments = arguments or tuple(text(value) for value in values)  # type: ignore[assignment]
    result = run(binary, "approx", *arguments)
    if wanted == "invalid":
        ok = result.returncode == 2 and not result.stdout and "tolerance" in result.stderr
    else:
        accepted = wanted == "accept"
        ok = (result.returncode == (0 if accepted else 1) and not result.stderr and
              result.stdout == ("within tolerance\n" if accepted else "outside tolerance\n"))
    if not ok:
        raise SystemExit(f"approx {arguments}: expected {wanted}; got exit={result.returncode}, "
                         f"stdout={result.stdout!r}, stderr={result.stderr!r}")
    return wanted


def random_fraction(rng: random.Random) -> Fraction:
    return Fraction(rng.randint(-2000, 2000), rng.choice([1, 2, 3, 7, 10, 100, 1000, 999, 1024]))


def generated(rng: random.Random, count: int) -> list[tuple[Fraction, Fraction, Fraction, Fraction]]:
    cases = []
    wide_numerators = [MINIMUM, MINIMUM + 1, -MAXIMUM // 2, -1, 0, 1, MAXIMUM // 2, MAXIMUM - 1, MAXIMUM]
    wide_denominators = [1, 2, 3, 2**62, MAXIMUM - 1, MAXIMUM]
    while len(cases) < count:
        kind = len(cases) % 4
        if kind == 0:
            # Unrelated values and tolerances.
            expected, answer = random_fraction(rng), random_fraction(rng)
            absolute = rng.choice([Fraction(0), abs(random_fraction(rng)) / 100])
            relative = rng.choice([Fraction(0), Fraction(rng.randint(0, 999), 1000)])
        elif kind in (1, 2):
            # An answer exactly on a bound, or one step either side of it.
            expected = random_fraction(rng)
            absolute = Fraction(rng.randint(0, 50), rng.choice([1, 10, 100, 1000, 3]))
            relative = Fraction(rng.randint(0, 99), rng.choice([100, 1000, 128]))
            use_relative = kind == 2 and expected != 0
            bound = relative * abs(expected) if use_relative else absolute
            if use_relative:
                absolute = Fraction(0)
            else:
                relative = Fraction(0)
            step = Fraction(1, rng.choice([10**6, 10**12, 10**18, MAXIMUM]))
            answer = expected + rng.choice([-1, 1]) * (bound + rng.choice([-step, Fraction(0), step]))
        else:
            # Full-width operands, including distances that are not representable.
            def wide() -> Fraction:
                return Fraction(rng.choice(wide_numerators), rng.choice(wide_denominators))
            expected, answer = wide(), wide()
            if rng.random() < 0.5:
                answer = expected + Fraction(rng.choice([-1, 1]), rng.choice(wide_denominators))
            absolute = rng.choice([Fraction(0), abs(wide()), Fraction(1, MAXIMUM)])
            relative = rng.choice([Fraction(0), Fraction(1, 10**18), Fraction(MAXIMUM - 1, MAXIMUM), Fraction(1, 2)])
        values = (expected, answer, absolute, relative)
        if all(representable(value) for value in values):
            cases.append(values)
    return cases


def cli_contracts(binary: Path) -> None:
    # Exact comparison is unchanged: check still rejects what approx accepts.
    result = run(binary, "check", "1/3", "0.333")
    if result.returncode != 1 or result.stdout != "not equivalent\n":
        raise SystemExit(f"approx must not change exact check: {result}")
    for arguments, label, position in [
        (("1/0", "1", "0", "0"), "expected value", 1),
        (("1", "2 +", "0", "0"), "answer", 3),
        (("1", "1", "1/0", "0"), "absolute tolerance", 1),
        (("1", "1", "0", "(1"), "relative tolerance", 2),
        (("9223372036854775807 + 1", "1", "0", "0"), "expected value", 20),
    ]:
        result = run(binary, "approx", *arguments)
        if (result.returncode != 2 or result.stdout or
                not result.stderr.startswith(f"error in {label} at byte {position}:")):
            raise SystemExit(f"approx error for {arguments} must name {label} at byte {position}: {result}")
    for arguments in [("approx",), ("approx", "1", "1", "0"), ("approx", "1", "1", "0", "0", "0")]:
        result = run(binary, *arguments)
        if result.returncode != 2 or result.stdout or "Usage:" not in result.stderr:
            raise SystemExit(f"approx argument-count contract failed for {arguments}: {result}")
    help_text = run(binary, "--help").stdout
    if "approx EXPECTED ANSWER ABSOLUTE RELATIVE" not in help_text:
        raise SystemExit("--help must describe approx")
    with open(os.devnull, "rb") as unwritable_output:
        for arguments in [("1", "1", "0", "0"), ("1", "2", "0", "0")]:
            result = subprocess.run([str(binary), "approx", *arguments], stdout=unwritable_output,
                                    stderr=subprocess.PIPE, text=True, timeout=5, check=False)
            if result.returncode != 2 or result.stderr != "error: could not write command output\n":
                raise SystemExit(f"approx failed-output contract failed for {arguments}: {result}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path, nargs="?",
                        help="axiom-math CLI; omit to check only the published worked examples")
    parser.add_argument("--cases", type=int, default=1200)
    args = parser.parse_args()
    if not 1 <= args.cases <= 20000:
        parser.error("--cases must be between 1 and 20000")
    rows = examples()
    check_examples(rows)
    if args.binary is None:
        print(f"PASS {len(rows)} worked examples recomputed with Python Fraction")
        return
    binary = args.binary.resolve(strict=True)
    for row in rows:
        expect_cli(binary, tuple(Fraction(cell) for cell in row[:4]), row[:4])  # type: ignore[arg-type]
    counts = {"accept": 0, "reject": 0, "invalid": 0}
    rng = random.Random(0x544F4C)
    for values in generated(rng, args.cases):
        counts[expect_cli(binary, values)] += 1
    for values in [(Fraction(5), Fraction(5), Fraction(-1, MAXIMUM), Fraction(0)),
                   (Fraction(5), Fraction(5), Fraction(0), Fraction(MAXIMUM, MAXIMUM - 1)),
                   (Fraction(0), Fraction(0), Fraction(0), Fraction(-1, MAXIMUM)),
                   (Fraction(0), Fraction(0), Fraction(MINIMUM), Fraction(0)),
                   (Fraction(1), Fraction(1), Fraction(0), Fraction(1)),
                   (Fraction(1), Fraction(0), Fraction(1, 2), Fraction(99, 98))]:
        counts[expect_cli(binary, values)] += 1
    cli_contracts(binary)
    print(f"PASS {len(rows)} worked examples and {sum(counts.values())} generated cases against Python Fraction "
          f"({counts['accept']} accepted, {counts['reject']} rejected, {counts['invalid']} invalid); approx CLI contracts")


if __name__ == "__main__":
    main()
