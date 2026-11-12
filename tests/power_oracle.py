"""Compare the CLI integer-power command with Python's independent Fraction."""
from fractions import Fraction
import random
import subprocess
import sys

binary = sys.argv[1]
rng = random.Random(20261112)
cases = [(rng.randint(-12, 12), rng.randint(1, 12), rng.randint(-20, 20))
         for _ in range(250)]
cases += [(-2, 1, 63), (2, 1, 63), (0, 1, 0), (0, 1, -1),
          (-1, 1, -(2 ** 63)), (-1, 1, 2 ** 63 - 1), (-(2 ** 63), 1, 1)]
for numerator, denominator, exponent in cases:
    result = subprocess.run([binary, "pow", f"{numerator}/{denominator}", str(exponent)],
                            capture_output=True, text=True, timeout=5)
    try:
        expected = Fraction(numerator, denominator) ** exponent
        fits = -(2 ** 63) <= expected.numerator < 2 ** 63 and expected.denominator < 2 ** 63
    except ZeroDivisionError:
        fits = False
    if fits:
        assert result.returncode == 0, (numerator, denominator, exponent, result.stderr)
        assert result.stdout.strip() == str(expected), (result.stdout, expected)
    else:
        assert result.returncode == 2, (numerator, denominator, exponent, result)
for exponent in ("", "+-1", "+-0", "1.5", "1x", "9223372036854775808", "-9223372036854775809"):
    assert subprocess.run([binary, "pow", "2", exponent], capture_output=True).returncode == 2
assert subprocess.check_output([binary, "pow", "1+1", "+3"], text=True).strip() == "8"
print(f"Integer-power oracle passed {len(cases)} rational cases and invalid-input checks.")
