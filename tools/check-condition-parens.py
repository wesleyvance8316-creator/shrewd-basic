#!/usr/bin/env python3
"""Coding rule: a conditional with two or more sub-conditions wraps EACH
sub-condition in parentheses, e.g. `if ((a) && (!b))`. Single-clause
conditions stay bare: `if (0 == x)`. Boolean operators lead wrapped
lines (enforced by clang-format's BreakBeforeBinaryOperators).

Heuristic checker for tools/check-style.sh: scans if/while/for
conditions, splits on top-level && / || and requires every operand of a
multi-operand expression to be fully parenthesized.
"""
import re
import sys
import glob


def split_top(expr):
    parts, depth, cur, i = [], 0, '', 0
    while i < len(expr):
        c = expr[i]
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
        if depth == 0 and expr.startswith('&&', i):
            parts.append(cur)
            cur = ''
            i += 2
            continue
        if depth == 0 and expr.startswith('||', i):
            parts.append(cur)
            cur = ''
            i += 2
            continue
        cur += c
        i += 1
    parts.append(cur)
    return [p.strip() for p in parts]


def wrapped(op):
    op = op.strip()
    if not (op.startswith('(') and op.endswith(')')):
        return False
    depth = 0
    for i, c in enumerate(op):
        if c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0 and i != len(op) - 1:
                return False
    return True


def main():
    violations = 0
    for f in sorted(glob.glob('library/**/*.c', recursive=True)
                    + glob.glob('tests/*.c')
                    + glob.glob('programs/*.c')
                    + glob.glob('tools/*.c')
                    + glob.glob('include/**/*.h', recursive=True)):
        lines = open(f, encoding='utf-8').read().split('\n')
        i = 0
        while i < len(lines):
            s = lines[i].strip()
            m = re.match(r'^(if|while|for)\s*\(', s)
            if not m:
                i += 1
                continue
            depth, expr, j, started = 0, '', i, False
            while j < len(lines) and j < i + 12:
                for ch in lines[j]:
                    if ch == '(':
                        depth += 1
                        if depth == 1:
                            started = True
                            continue
                    elif ch == ')':
                        depth -= 1
                        if depth == 0:
                            break
                    if started:
                        expr += ch
                if depth == 0:
                    break
                expr += ' '
                j += 1
            cond = expr.split(';')[1] if (m.group(1) == 'for'
                                           and expr.count(';') >= 1) else expr
            ops = split_top(cond)
            if len(ops) >= 2 and not all(wrapped(o) for o in ops):
                print(f"{f}:{i+1}: sub-conditions need parentheses: {s[:64]}")
                violations += 1
            i = j + 1
    if violations:
        print(f"condition-parens: {violations} violation(s)")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
