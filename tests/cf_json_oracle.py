#!/usr/bin/env python3
"""Differential oracle for axiom-math `cf` and `--json` analytical telemetry."""

from __future__ import annotations

from fractions import Fraction
import json
from pathlib import Path
import random
import subprocess
import sys


def continued_fraction(f: Fraction) -> tuple[list[int], list[str]]:
    num, den = f.numerator, f.denominator
    coeffs: list[int] = []
    while den != 0:
        q = num // den  # Python floor division matches Euclidean continued fraction
        r = num - q * den
        coeffs.append(q)
        num, den = den, r
    convs: list[str] = []
    h0, h1 = 1, coeffs[0]
    g0, g1 = 0, 1
    convs.append(str(h1))
    for a in coeffs[1:]:
        h0, h1 = h1, a * h1 + h0
        g0, g1 = g1, a * g1 + g0
        convs.append(f"{h1}/{g1}")
    return coeffs, convs


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("Usage: cf_json_oracle.py PATH_TO_AXIOM_MATH")
    binary = Path(sys.argv[1]).resolve(strict=True)

    cases: list[tuple[str, Fraction]] = [
        ("0", Fraction(0)),
        ("1", Fraction(1)),
        ("-1", Fraction(-1)),
        ("355/113", Fraction(355, 113)),
        ("-7/3", Fraction(-7, 3)),
        ("1/3", Fraction(1, 3)),
        ("1/6", Fraction(1, 6)),
        ("1/7", Fraction(1, 7)),
        ("22/7", Fraction(22, 7)),
        ("-9223372036854775808", Fraction(-9223372036854775808)),
        ("-9223372036854775808/3", Fraction(-9223372036854775808, 3)),
        ("9223372036854775807/9223372036854775806", Fraction(9223372036854775807, 9223372036854775806)),
    ]

    rng = random.Random(0xCF2026)
    for _ in range(250):
        a = rng.randint(-10000, 10000)
        b = rng.randint(1, 5000)
        c = rng.randint(-10000, 10000)
        d = rng.randint(1, 5000)
        op = rng.choice(["+", "-", "*", "/"])
        fa, fc = Fraction(a, b), Fraction(c, d)
        if op == "/" and fc == 0:
            op = "+"
        expr = f"({a}/{b}) {op} ({c}/{d})"
        val = fa + fc if op == "+" else fa - fc if op == "-" else fa * fc if op == "*" else fa / fc
        cases.append((expr, val))

    for expr, exact in cases:
        coeffs, convs = continued_fraction(exact)
        proc = subprocess.run([str(binary), "--json", "cf", expr], capture_output=True, text=True, check=False)
        if proc.returncode != 0 or proc.stderr:
            raise SystemExit(f"cf --json failed on {expr!r}: rc={proc.returncode} stderr={proc.stderr!r}")
        data = json.loads(proc.stdout)
        r = data["result"]
        if r["numerator"] != exact.numerator or r["denominator"] != exact.denominator:
            raise SystemExit(f"rational mismatch on {expr!r}: {r}")
        if r["continued_fraction"] != coeffs:
            raise SystemExit(f"CF mismatch on {expr!r}: got {r['continued_fraction']}, expected {coeffs}")
        if r["convergents"] != convs:
            raise SystemExit(f"convergents mismatch on {expr!r}: got {r['convergents']}, expected {convs}")

    # Verify suffix --json on eval, explain, check, approx, pow + repeating decimal metadata
    p_expl = subprocess.run([str(binary), "explain", "(1/3 + 1/6) * 4", "--json"], capture_output=True, text=True, check=False)
    d_expl = json.loads(p_expl.stdout)
    assert d_expl["command"] == "explain" and d_expl["result"]["canonical"] == "2" and len(d_expl["steps"]) == 4

    p_dec = json.loads(subprocess.run([str(binary), "cf", "1/7", "--json"], capture_output=True, text=True, check=True).stdout)
    assert p_dec["result"]["decimal"]["text"] == "0.(142857)" and p_dec["result"]["decimal"]["period_length"] == 6

    p_chk = subprocess.run([str(binary), "--json", "check", "1/2 + 1/3", "5/6"], capture_output=True, text=True, check=False)
    assert p_chk.returncode == 0 and json.loads(p_chk.stdout)["equivalent"] is True

    p_apx = subprocess.run([str(binary), "--json", "approx", "10", "9.9", "0.2", "0"], capture_output=True, text=True, check=False)
    assert p_apx.returncode == 0 and json.loads(p_apx.stdout)["accepted"] is True

    p_pow = subprocess.run([str(binary), "pow", "2/3", "-2", "--json"], capture_output=True, text=True, check=False)
    assert p_pow.returncode == 0 and json.loads(p_pow.stdout)["result"]["canonical"] == "9/4"

    # Also test plain text `cf` CLI output
    p_plain = subprocess.run([str(binary), "cf", "355/113"], capture_output=True, text=True, check=False)
    assert p_plain.returncode == 0 and "[3; 7, 16]" in p_plain.stdout and "3, 22/7, 355/113" in p_plain.stdout

    print(f"PASS {len(cases)} continued-fraction + JSON telemetry cases verified against Python Fraction")


if __name__ == "__main__":
    main()
