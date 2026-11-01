#!/usr/bin/env python3
"""Compare the CLI against Python's independent arbitrary-precision Fraction.

The fixed seed makes failures reproducible. Inputs are synthetic, bounded, and
contain only the supported arithmetic grammar. This is test tooling, not a
runtime dependency of AxiomMath.
"""

from __future__ import annotations

import argparse
from fractions import Fraction
import os
from pathlib import Path
import random
import re
import subprocess


def canonical(value: Fraction) -> str:
    return str(value.numerator) if value.denominator == 1 else str(value)


def expression(rng: random.Random, depth: int) -> tuple[str, Fraction]:
    if depth == 0 or rng.random() < 0.3:
        number = rng.randint(-30, 30)
        if rng.random() < 0.4:
            magnitude = abs(number)
            text = f"{'-' if number < 0 else ''}{magnitude // 10}.{magnitude % 10}"
            return text, Fraction(text)
        return str(number), Fraction(number)
    left_text, left = expression(rng, depth - 1)
    right_text, right = expression(rng, depth - 1)
    operation = rng.choice("+-*/")
    if operation == "/" and right == 0:
        operation = "+"
    if operation == "+":
        value = left + right
    elif operation == "-":
        value = left - right
    elif operation == "*":
        value = left * right
    else:
        value = left / right
    # Keep every subtree representable in the documented int64 result range.
    # Python itself remains an arbitrary-precision arithmetic oracle.
    if abs(value.numerator) > 2**63 - 1 or value.denominator > 2**63 - 1:
        return "1", Fraction(1)
    return f"({left_text} {operation} {right_text})", value


def run(binary: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(binary), *arguments], text=True, capture_output=True, timeout=5, check=False)


def cli_contracts(binary: Path) -> None:
    for left, right, code, output in [
        ("1/3 + 1/6", "0.5", 0, "equivalent\n"),
        ("1/3", "0.333333", 1, "not equivalent\n"),
    ]:
        result = run(binary, "check", left, right)
        if result.returncode != code or result.stdout != output or result.stderr:
            raise SystemExit(f"Equivalence CLI contract failed: {left!r}, {right!r}: {result}")

    result = run(binary, "explain", "(1/3 + 1/6) * 4")
    expected_steps = (
        "1. 1/3 = 1/3\n"
        "2. 1/6 = 1/6\n"
        "3. 1/3 + 1/6 = 1/2\n"
        "4. (1/3 + 1/6) * 4 = 2\n"
        "Result: 2\n"
    )
    if result.returncode != 0 or result.stdout != expected_steps or result.stderr:
        raise SystemExit(f"Worked-calculation CLI contract failed: {result}")

    for flag in ("--help", "-h"):
        result = run(binary, flag)
        if result.returncode != 0 or "Usage:" not in result.stdout or result.stderr:
            raise SystemExit(f"Help CLI contract failed for {flag}: {result}")
    result = run(binary, "--version")
    if result.returncode != 0 or not re.fullmatch(r"AxiomMath \d+\.\d+\.\d+\n", result.stdout) or result.stderr:
        raise SystemExit(f"Version CLI contract failed: {result}")

    for arguments in [
        (), ("unknown",), ("eval",), ("eval", "1", "2"),
        ("explain",), ("explain", "1", "2"),
        ("check", "1"), ("check", "1", "1", "1"), ("--version", "extra"),
    ]:
        result = run(binary, *arguments)
        if result.returncode != 2 or result.stdout or "Usage:" not in result.stderr:
            raise SystemExit(f"Argument-count CLI contract failed for {arguments}: {result}")

    for text in ["1/0", "1 +", "2^3", "9223372036854775807 + 1"]:
        result = run(binary, "eval", text)
        if result.returncode != 2 or result.stdout or not result.stderr.strip():
            raise SystemExit(f"Error CLI contract failed for {text!r}: {result}")

    result = run(binary, "eval", "1/0")
    lines = result.stderr.splitlines()
    if len(lines) != 3 or not lines[0].startswith("error in expression at byte 1:") or lines[1:] != ["1/0", " ^"]:
        raise SystemExit(f"Division-by-zero source location is incorrect: {result}")

    for left, right, label in [("1", "2/0", "right"), ("2/0", "1", "left")]:
        result = run(binary, "check", left, right)
        lines = result.stderr.splitlines()
        if result.returncode != 2 or result.stdout or len(lines) != 3 or not lines[0].startswith(f"error in {label} expression at byte 1:") or lines[1:] != ["2/0", " ^"]:
            raise SystemExit(f"Equivalence error operand/location is incorrect: {result}")

    # An expression is untrusted terminal text. The source excerpt must never
    # replay an escape sequence or other non-printing byte from user input.
    for control in ("\x1b", "\x07", "\x7f"):
        result = run(binary, "eval", f"1+{control}[31m2")
        if result.returncode != 2 or result.stdout or control in result.stderr or any(ord(character) < 32 and character != "\n" for character in result.stderr):
            raise SystemExit(f"Unsafe control byte in error output: {result}")

    # A read-only descriptor deterministically rejects writes on both Linux
    # and macOS, including buffered failures that surface only on flush.
    with open(os.devnull, "rb") as unwritable_output:
        for arguments in [
            ("eval", "1+2"), ("explain", "1+2"),
            ("check", "1", "1"), ("check", "1", "2"),
            ("--help",), ("-h",), ("--version",),
        ]:
            result = subprocess.run(
                [str(binary), *arguments], stdout=unwritable_output,
                stderr=subprocess.PIPE, text=True, timeout=5, check=False,
            )
            if result.returncode != 2 or result.stderr != "error: could not write command output\n":
                raise SystemExit(f"Failed-output CLI contract failed for {arguments}: {result}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--cases", type=int, default=300)
    args = parser.parse_args()
    if not 1 <= args.cases <= 10000:
        parser.error("--cases must be between 1 and 10000")
    binary = args.binary.resolve(strict=True)
    rng = random.Random(0x4158494F)
    for index in range(args.cases):
        text, expected = expression(rng, 4)
        result = run(binary, "eval", text)
        if result.returncode != 0 or result.stdout.strip() != canonical(expected):
            raise SystemExit(f"Case {index} failed: {text}\nExpected {canonical(expected)}; got {result.stdout!r}; stderr={result.stderr!r}; exit={result.returncode}")
    cli_contracts(binary)
    print(f"PASS {args.cases} seeded expressions against Python Fraction; CLI commands, explanations, error positions and terminal-output contracts")


if __name__ == "__main__":
    main()
