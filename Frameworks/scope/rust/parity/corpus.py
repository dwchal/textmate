#!/usr/bin/env python3
"""Build a selector×scope corpus from installed bundles plus random fuzz.

Output lines: selector \t left-scope \t right-scope
Usage: corpus.py <seed> <bundle-dir>...
"""
import os, plistlib, random, re, sys

seed, dirs = int(sys.argv[1]), sys.argv[2:]
rng = random.Random(seed)
selectors, scope_names = set(), set()

def walk(obj, key=None):
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k in ("injections", "repository") and isinstance(v, dict) and k == "injections":
                selectors.update(v.keys())
            walk(v, k)
    elif isinstance(obj, list):
        for v in obj:
            walk(v, key)
    elif isinstance(obj, str):
        if key in ("scope", "scopeSelector", "injectionSelector", "scopeAttributes"):
            selectors.add(obj)
        elif key in ("scopeName", "name", "contentName"):
            scope_names.update(a for a in obj.split() if not a.startswith("$"))

for d in dirs:
    for root, _, files in os.walk(d):
        for f in files:
            if f.endswith((".plist", ".tmLanguage", ".tmPreferences", ".tmSnippet", ".tmCommand", ".tmMacro", ".tmTheme", ".tmDragCommand")):
                p = os.path.join(root, f)
                try:
                    with open(p, "rb") as fh:
                        walk(plistlib.load(fh))
                except Exception:
                    # old-style ASCII plists: scope = 'source.ruby';
                    txt = open(p, "rb").read().decode("utf-8", "replace")
                    selectors.update(m.group(2) for m in re.finditer(r"\b(scope|scopeSelector)\s*=\s*(?:'|\")([^'\"]*)", txt))
                    scope_names.update(a for m in re.finditer(r"\b(?:scopeName|name)\s*=\s*(?:'|\")([^'\"]*)", txt) for a in m.group(1).split())

scope_names = sorted(n for n in scope_names if re.fullmatch(r"[\w.+\-]+", n))
selectors = sorted(selectors)
aux = ["dyn.selection", "dyn.caret.begin", "dyn.caret.mixed.columnar", "attr.os-version.10.15", "attr.rev-path.md.README.Users", "attr.scm.git"]
fuzz_alphabet = list("abcxyz.*>^$ ,|&-()LRB:\t_+") + ["foo", "bar", "source", "text", "é", "\x00", "dyn.", "attr."]

def rand_scope():
    n = rng.randint(0, 7)
    atoms = [rng.choice(scope_names) for _ in range(n)]
    for _ in range(rng.randint(0, 2)):
        atoms.append(rng.choice(aux))
    return " ".join(atoms)

def rand_selector():
    if selectors and rng.random() < 0.6:
        return rng.choice(selectors)
    if rng.random() < 0.5:
        # mutate a real selector
        s = list(rng.choice(selectors)) if selectors else []
        for _ in range(rng.randint(1, 4)):
            pos = rng.randint(0, len(s))
            s[pos:pos + rng.randint(0, 2)] = rng.choice(fuzz_alphabet)
        return "".join(s)
    return "".join(rng.choice(fuzz_alphabet) for _ in range(rng.randint(0, 25)))

def derived_scope(sel):
    # scopes built from a selector's own atoms so matches actually happen
    atoms = re.findall(r"[\w.+\-*]+", sel)
    picks = [a.replace("*", rng.choice(["x", "foo", ""])) for a in atoms if rng.random() < 0.7]
    ext = [a + "." + rng.choice(["ruby", "php", "x.y"]) if rng.random() < 0.3 else a for a in picks]
    base = [rng.choice(scope_names)] if scope_names and rng.random() < 0.5 else []
    return " ".join(base + ext + ([rng.choice(aux)] if rng.random() < 0.3 else []))

out = sys.stdout
for sel in selectors:  # every real selector, against several scopes
    for _ in range(8):
        l = derived_scope(sel) if rng.random() < 0.6 else rand_scope()
        r = l if rng.random() < 0.6 else (derived_scope(sel) if rng.random() < 0.5 else rand_scope())
        out.write(f"{sel}\t{l}\t{r}\n")
for _ in range(200000):
    sel = rand_selector()
    l = derived_scope(sel) if rng.random() < 0.5 else rand_scope()
    r = l if rng.random() < 0.5 else rand_scope()
    out.write(f"{sel.replace(chr(10), ' ')}\t{l}\t{r}\n")
print(f"{len(selectors)} bundle selectors, {len(scope_names)} scope names", file=sys.stderr)
