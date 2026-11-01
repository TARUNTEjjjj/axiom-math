#!/usr/bin/env python3
"""Check full-width arithmetic and rejected results against Python Fraction.

Every operand is independently reduced and representable before it reaches the
CLI. Only the outer operation may overflow: a result fits when its numerator is
in [-2**63, 2**63-1] and its positive denominator is at most 2**63-1. Python's
arbitrary-precision integers supply the expected result without sharing the C++
implementation's 128-bit intermediates or reduction code.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
from pathlib import Path
import random
import subprocess


MINIMUM = -(2**63)
MAXIMUM = 2**63 - 1


def representable(value: Fraction) -> bool:
    return MINIMUM <= value.numerator <= MAXIMUM and value.denominator <= MAXIMUM


def operand(value: Fraction) -> str:
    if not representable(value):
        raise AssertionError(f"Oracle generated an unrepresentable operand: {value}")
    return f"({value.numerator}/{value.denominator})"


def cases(count: int) -> list[tuple[Fraction, str, Fraction]]:
    low = Fraction(MINIMUM)
    high = Fraction(MAXIMUM)
    examples = [
        (high, "+", Fraction(1)),
        (low, "-", Fraction(1)),
        (high, "*", Fraction(2)),
        (low, "/", Fraction(-1)),
        (Fraction(1, MAXIMUM), "*", Fraction(1, MAXIMUM)),
        (low, "-", low),
        (high, "+", low),
        (high / 2, "+", high / 2),
        (high / 2, "*", Fraction(2, MAXIMUM)),
        (low, "/", low),
        (Fraction(1), "/", low),
        (Fraction(0), "/", low),
        (Fraction(1), "/", Fraction(0)),
        (Fraction(0), "/", Fraction(0)),
        (Fraction(MAXIMUM, MAXIMUM - 1), "-", Fraction(MAXIMUM, MAXIMUM - 1)),
        (Fraction(MAXIMUM, MAXIMUM - 1), "*", Fraction(MAXIMUM - 1, MAXIMUM)),
    ]
    rng = random.Random(0x57494445)
    numerators = [MINIMUM, MINIMUM + 1, MINIMUM + 2, -MAXIMUM // 2,
                  -3, -1, 0, 1, 3, MAXIMUM // 2, MAXIMUM - 2, MAXIMUM - 1, MAXIMUM]
    denominators = [1, 2, 3, 2**62, MAXIMUM - 2, MAXIMUM - 1, MAXIMUM]

    def fraction() -> Fraction:
        numerator = rng.choice(numerators) if rng.randrange(3) else rng.randint(MINIMUM, MAXIMUM)
        denominator = rng.choice(denominators) if rng.randrange(3) else rng.randint(1, MAXIMUM)
        return Fraction(numerator, denominator)

    while len(examples) < count:
        left = fraction()
        operation = "+-*/"[len(examples) % 4]
        # Equal operands deliberately exercise subtraction/division cancellation
        # as well as overflow from doubling/squaring full-width fractions.
        right = left if rng.randrange(4) == 0 else fraction()
        examples.append((left, operation, right))
    return examples


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--cases", type=int, default=512)
    args = parser.parse_args()
    if not 16 <= args.cases <= 10000:
        parser.error("--cases must be between 16 and 10000")
    binary = args.binary.resolve(strict=True)
    counts = {"exact": 0, "overflow": 0, "domain": 0}
    for index, (left, operation, right) in enumerate(cases(args.cases)):
        text = f"{operand(left)} {operation} {operand(right)}"
        if operation == "/" and right == 0:
            category = "domain"
            expected = "division by zero"
        else:
            if operation == "+":
                value = left + right
            elif operation == "-":
                value = left - right
            elif operation == "*":
                value = left * right
            else:
                value = left / right
            category = "exact" if representable(value) else "overflow"
            expected = str(value) if category == "exact" else "exceeds the signed 64-bit range"
        result = subprocess.run([str(binary), "eval", text], text=True,
                                capture_output=True, timeout=5, check=False)
        valid = (result.returncode == 0 and result.stdout == expected + "\n" and not result.stderr
                 if category == "exact" else
                 result.returncode == 2 and not result.stdout and expected in result.stderr)
        if not valid:
            raise SystemExit(f"Wide case {index} failed: {text}\nExpected {category}: {expected}; "
                             f"exit={result.returncode}; stdout={result.stdout!r}; stderr={result.stderr!r}")
        counts[category] += 1
    print(f"PASS {args.cases} full-width Fraction cases: {counts['exact']} exact, "
          f"{counts['overflow']} overflow, {counts['domain']} division-by-zero")


if __name__ == "__main__":
    main()
