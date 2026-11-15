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


# How tightly printed text binds: a sum or difference, a product or quotient,
# a signed operand, and a number or parenthesized group.
SUM, PRODUCT, SIGNED, ATOM = range(4)
LEVEL = {"+": SUM, "-": SUM, "*": PRODUCT, "/": PRODUCT}
# Separators between tokens, including every ASCII whitespace byte the parser accepts.
GAPS = ["", "", " ", " ", " ", "  ", "\t", "\n", "\r", "\f", "\v"]


def gap(rng: random.Random) -> str:
    return rng.choice(GAPS)


def group(rng: random.Random, text: str) -> str:
    return f"({gap(rng)}{text}{gap(rng)})"


def expression(rng: random.Random, depth: int, leaf_chance: float = 0.25) -> tuple[str, Fraction, int]:
    """Generate an expression tree and its exact value.

    The text has only the parentheses that precedence and left associativity
    require, plus some redundant ones, so the parser's grouping rules decide
    its value. Also returns how tightly the text binds.
    """
    choice = rng.random()
    if depth == 0 or choice < leaf_chance:
        number = rng.randint(-30, 30)
        if rng.random() < 0.4:
            magnitude = abs(number)
            text = f"{'-' if number < 0 else ''}{magnitude // 10}.{magnitude % 10}"
        else:
            text = str(number)
        value = Fraction(text)
        level = SIGNED if number < 0 else ATOM
    elif choice < leaf_chance + 0.15:
        operand_text, operand, operand_level = expression(rng, depth - 1)
        sign = rng.choice("+-")
        # A sign applies only to the operand that follows it.
        if operand_level < SIGNED:
            operand_text = group(rng, operand_text)
        text = f"{sign}{gap(rng)}{operand_text}"
        value = -operand if sign == "-" else operand
        level = SIGNED
    else:
        left_text, left, left_level = expression(rng, depth - 1)
        right_text, right, right_level = expression(rng, depth - 1)
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
        level = LEVEL[operation]
        # Operators of one level associate to the left, so a right operand at
        # that level needs parentheses and a left operand does not.
        if left_level < level:
            left_text = group(rng, left_text)
        if right_level <= level:
            right_text = group(rng, right_text)
        text = f"{left_text}{gap(rng)}{operation}{gap(rng)}{right_text}"
    # Keep every subtree representable in the documented int64 result range.
    # Python itself remains an arbitrary-precision arithmetic oracle.
    if abs(value.numerator) > 2**63 - 1 or value.denominator > 2**63 - 1:
        return "1", Fraction(1), ATOM
    if rng.random() < 0.1:
        return group(rng, text), value, ATOM
    return text, value, level


def run(binary: Path, *arguments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run([str(binary), *arguments], text=True, capture_output=True, timeout=5, check=False)


def source_excerpt(text: str, position: int) -> list[str]:
    """The CLI's excerpt of an invalid expression and its caret line: up to 90
    bytes starting 45 bytes before the error, "..." where input is cut off, and
    the caret under the byte at the error position."""
    begin = max(position - 45, 0)
    prefix = "..." if begin else ""
    suffix = "..." if begin + 90 < len(text) else ""
    return [prefix + text[begin:begin + 90] + suffix, " " * (len(prefix) + position - begin) + "^"]


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

    # Every step quotes its whole subexpression, so the steps of a long chain
    # repeat its text: 1.8 MB for 4,096 bytes padded with spaces, and 66,048
    # bytes for a sum of 257 ones. explain prints the first steps, which quote
    # at most 65,536 bytes, counts the others, and still prints the result.
    for spaces, terms, shown in [(3073, 511, 21), (0, 256, 255)]:
        chain = "1" + " " * spaces + "+1" * terms
        expected_output = "".join(
            f"{index}. {chain[:1 + spaces + 2 * index]} = {index + 1}\n" for index in range(1, shown + 1)
        ) + (
            f"Omitted steps: {terms - shown} (the steps of one explanation quote at most 65536 bytes)\n"
            f"Result: {terms + 1}\n"
        )
        result = run(binary, "explain", chain)
        if result.returncode != 0 or result.stdout != expected_output or result.stderr:
            raise SystemExit(f"Explanation-limit CLI contract failed for {terms} sums: exit={result.returncode}, "
                             f"stdout={len(result.stdout)} bytes ending {result.stdout[-120:]!r}, stderr={result.stderr[:200]!r}")
        result = run(binary, "eval", chain)
        if result.returncode != 0 or result.stdout != f"{terms + 1}\n" or result.stderr:
            raise SystemExit(f"Untraced evaluation of the long chain failed: {result}")

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

    # In longer input the excerpt is bounded and marked where it is cut off,
    # and the caret still points at the byte where the error is.
    total = "(" + "+".join(str(number) for number in range(1, 21)) + ")"
    for text, position, message in [
        (f"{total} / (5-5)", 53, "division by zero"),
        ("1 + " * 30 + "1/0" + " + 1" * 30, 121, "division by zero"),
        ("@" + " " * 4000 + "1", 0, "Expected a number or '('."),
        ("1" + " + 1" * 30 + " +", 123, "Expected a number or '('."),
        (" " * 4096 + "@" + " " * 903, 4096, "Expression exceeds the 4096-byte limit."),
    ]:
        result = run(binary, "eval", text)
        lines = result.stderr.splitlines()
        expected = [f"error in expression at byte {position}: {message}", *source_excerpt(text, position)]
        caret = lines[2].find("^") if len(lines) == 3 else -1
        pointed = position == len(text) or (0 <= caret < len(lines[1]) and lines[1][caret] == text[position])
        if result.returncode != 2 or result.stdout or lines != expected or not pointed:
            raise SystemExit(f"Long-input source excerpt is incorrect for a {len(text)}-byte input: "
                             f"exit={result.returncode}, stderr={result.stderr[:400]!r}")

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
    # Each byte outside printable ASCII, such as the C1 control CSI (0x9b) raw
    # or UTF-8 encoded, becomes one space, so the caret stays aligned.
    for text, excerpt in [
        (b"1+\x9b2J", b"1+ 2J"),
        (b"1+\xc2\x9b31m2\xc2\x9d0;x\xc2\x9c", b"1+  31m2  0;x  "),
        (b"1+\xc3\xa9", b"1+  "),
    ]:
        raw = subprocess.run([str(binary), "eval", text], capture_output=True, timeout=5, check=False)
        expected = [b"error in expression at byte 2: Expected a number or '('.", excerpt, b"  ^", b""]
        if raw.returncode != 2 or raw.stdout or raw.stderr.split(b"\n") != expected:
            raise SystemExit(f"Unsafe or misaligned non-ASCII error output for {text!r}: {raw}")
    # Calculation steps quote the input, including any whitespace the parser
    # accepts. Only spaces may reach the terminal.
    for text, steps in [
        ("7 *\r3 +\n1", "1. 7 * 3 = 21\n2. 7 * 3 + 1 = 22\nResult: 22\n"),
        ("1\v+\f2\t*3", "1. 2 *3 = 6\n2. 1 + 2 *3 = 7\nResult: 7\n"),
    ]:
        result = run(binary, "explain", text)
        if result.returncode != 0 or result.stdout != steps or result.stderr:
            raise SystemExit(f"Unsafe whitespace in explanation steps for {text!r}: {result}")

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
        # The whole expression is an operation, not a single literal.
        text, expected, _ = expression(rng, 4, leaf_chance=0)
        text = f"{gap(rng)}{text}{gap(rng)}"
        result = run(binary, "eval", text)
        if result.returncode != 0 or result.stdout.strip() != canonical(expected):
            raise SystemExit(f"Case {index} failed: {text!r}\nExpected {canonical(expected)}; got {result.stdout!r}; stderr={result.stderr!r}; exit={result.returncode}")
    cli_contracts(binary)
    print(f"PASS {args.cases} seeded expressions against Python Fraction; CLI commands, explanations, error positions and terminal-output contracts")


if __name__ == "__main__":
    main()
