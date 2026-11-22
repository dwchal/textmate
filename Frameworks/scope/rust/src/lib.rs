//! TextMate scope selectors, ported from the C++ in `Frameworks/scope/src`
//! (`parse.cc`, `match.cc`, `types.cc`).
//!
//! The port is deliberately literal: parsing, `to_s` output and match ranks
//! must be byte-for-byte and bit-for-bit identical to the C++ implementation,
//! because bundles, themes and settings rely on the exact ranking.
//!
//! The scope *paths* being matched (`scope::scope_t`) stay in C++. They are
//! hot, reference-counted linked lists used throughout the editor, so the
//! matcher walks them through the [`NodeAccess`] trait instead of copying.
//!
//! Grammar (from parse.cc):
//!
//! ```text
//! atom:         «string» | '*'
//! scope:        «atom» ('.' «atom»)*
//! path:         '^'? «scope» ('>'? «scope»)* '$'?
//! group:        '(' «selector» ')'
//! filter:       ("L:"|"R:"|"B:") («group» | «path»)
//! expression:   '-'? («filter» | «group» | «path»)
//! composite:    «expression» ([|&-] «expression»)*
//! selector:     «composite» (',' «composite»)*
//! ```

pub mod ffi;

// =========
// = Types =
// =========

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ScopeSel {
    pub atoms: Vec<u8>,
    pub anchor_to_previous: bool,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Path {
    pub scopes: Vec<ScopeSel>,
    pub anchor_to_bol: bool,
    pub anchor_to_eol: bool,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Any {
    Path(Path),
    Group(Selector),
    /// `side` is the raw byte the parser saw: `L`, `R`, `B` (or 0, see `parse_char`).
    Filter { side: u8, selector: Box<Any> },
}

/// Operator bytes match the C++ `expression_t::op_t` values.
pub const OP_NONE: u8 = 0;
pub const OP_OR: u8 = b'|';
pub const OP_AND: u8 = b'&';
pub const OP_MINUS: u8 = b'-';

#[derive(Clone, Debug, PartialEq)]
pub struct Expression {
    pub op: u8,
    pub negate: bool,
    pub selector: Box<Any>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Composite {
    pub expressions: Vec<Expression>,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Selector {
    pub composites: Vec<Composite>,
}

// ===========
// = Parsing =
// ===========

struct Parser<'a> {
    s: &'a [u8],
    it: usize,
}

impl<'a> Parser<'a> {
    fn ws(&mut self) -> bool {
        // C++: strchr(" \t", *it) — note strchr also matches the NUL terminator.
        while self.it < self.s.len() && matches!(self.s[self.it], b' ' | b'\t' | 0) {
            self.it += 1;
        }
        true
    }

    /// C++ `parse_char`: uses `strchr(set, *it)`, which also matches a NUL byte.
    fn parse_char(&mut self, set: &[u8]) -> Option<u8> {
        let c = *self.s.get(self.it)?;
        if c == 0 || set.contains(&c) {
            self.it += 1;
            Some(c)
        } else {
            None
        }
    }

    fn eat(&mut self, set: &[u8]) -> bool {
        self.parse_char(set).is_some()
    }

    fn parse_scope(&mut self, res: &mut ScopeSel) -> bool {
        res.anchor_to_previous = self.eat(b">") && self.ws();

        let from = self.it;
        loop {
            // Compiled with -funsigned-char, so bytes ≥ 0x80 are always accepted.
            match self.s.get(self.it) {
                None => break,
                Some(&c) if !c.is_ascii_alphanumeric() && c != b'*' && c < 0x80 => break,
                _ => {}
            }
            while let Some(&c) = self.s.get(self.it) {
                if c.is_ascii_alphanumeric() || matches!(c, b'_' | b'-' | b'+' | b'*') || c > 0x7F {
                    self.it += 1;
                } else {
                    break;
                }
            }
            if !self.eat(b".") {
                break;
            }
        }
        res.atoms.extend_from_slice(&self.s[from..self.it]);

        from != self.it
    }

    fn parse_path(&mut self) -> Path {
        let mut res = Path::default();
        res.anchor_to_bol = self.eat(b"^") && self.ws();

        loop {
            let mut scope = ScopeSel::default();
            if !self.parse_scope(&mut scope) {
                break;
            }
            res.scopes.push(scope);
            self.ws();
        }

        res.anchor_to_eol = self.eat(b"$");
        res
    }

    fn parse_group(&mut self) -> Option<Any> {
        let bt = self.it;
        if self.eat(b"(") {
            let mut selector = Selector::default();
            if self.parse_selector(&mut selector) && self.ws() && self.eat(b")") {
                return Some(Any::Group(selector));
            }
        }
        self.it = bt;
        None
    }

    fn parse_filter(&mut self) -> Option<Any> {
        let bt = self.it;
        if let Some(side) = self.parse_char(b"LRB") {
            if self.eat(b":") && self.ws() {
                let inner = match self.parse_group() {
                    Some(group) => group,
                    None => Any::Path(self.parse_path()),
                };
                return Some(Any::Filter { side, selector: Box::new(inner) });
            }
        }
        self.it = bt;
        None
    }

    fn parse_expression(&mut self, op: u8) -> Expression {
        let negate = self.eat(b"-") && self.ws();
        let selector = self
            .parse_filter()
            .or_else(|| self.parse_group())
            .unwrap_or_else(|| Any::Path(self.parse_path()));
        Expression { op, negate, selector: Box::new(selector) }
    }

    fn parse_composite(&mut self, res: &mut Composite) -> bool {
        // An expression always parses (a path may be empty), so this always succeeds.
        let mut op = OP_NONE;
        loop {
            res.expressions.push(self.parse_expression(op));
            self.ws();
            match self.parse_char(b"&|-") {
                Some(c) => op = c,
                None => break,
            }
            self.ws();
        }
        true
    }

    fn parse_selector(&mut self, res: &mut Selector) -> bool {
        self.ws();
        loop {
            let mut composite = Composite::default();
            if !self.parse_composite(&mut composite) {
                break;
            }
            res.composites.push(composite);
            self.ws();
            if !self.eat(b",") {
                break;
            }
            self.ws();
        }
        !res.composites.is_empty()
    }
}

impl Selector {
    /// Parse a selector the way `scope::parse::selector` does. Never fails:
    /// unparsable trailing input is ignored, exactly like the C++ parser.
    pub fn parse(s: &[u8]) -> Selector {
        let mut p = Parser { s, it: 0 };
        p.ws();
        let mut res = Selector::default();
        p.parse_selector(&mut res);
        res
    }
}

// ===========
// = to_s    =
// ===========

/// C++ `join`: the separator is only emitted once the accumulated string is non-empty.
fn join<T>(items: &[T], sep: &[u8], out: &mut Vec<u8>, f: impl Fn(&T, &mut Vec<u8>)) {
    let start = out.len();
    for item in items {
        if out.len() != start {
            out.extend_from_slice(sep);
        }
        f(item, out);
    }
}

impl ScopeSel {
    fn write(&self, out: &mut Vec<u8>) {
        if self.anchor_to_previous {
            out.extend_from_slice(b"> ");
        }
        out.extend_from_slice(&self.atoms);
    }
}

impl Path {
    fn write(&self, out: &mut Vec<u8>) {
        if self.anchor_to_bol {
            out.extend_from_slice(b"^ ");
        }
        join(&self.scopes, b" ", out, ScopeSel::write);
        if self.anchor_to_eol {
            out.extend_from_slice(b" $");
        }
    }
}

impl Any {
    fn write(&self, out: &mut Vec<u8>) {
        match self {
            Any::Path(p) => p.write(out),
            Any::Group(s) => {
                out.push(b'(');
                s.write(out);
                out.push(b')');
            }
            Any::Filter { side, selector } => {
                // text::format("%c:") goes through a C string, so a NUL side yields "".
                if *side != 0 {
                    out.push(*side);
                    out.push(b':');
                }
                selector.write(out);
            }
        }
    }
}

impl Expression {
    fn write(&self, out: &mut Vec<u8>) {
        if self.op != OP_NONE {
            out.push(self.op);
            out.push(b' ');
        }
        if self.negate {
            out.push(b'-');
        }
        self.selector.write(out);
    }
}

impl Composite {
    fn write(&self, out: &mut Vec<u8>) {
        join(&self.expressions, b" ", out, Expression::write);
    }
}

impl Selector {
    pub fn write(&self, out: &mut Vec<u8>) {
        join(&self.composites, b", ", out, Composite::write);
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let mut out = Vec::new();
        self.write(&mut out);
        out
    }
}

// ============
// = Matching =
// ============

/// Read-only view of a scope path: a linked list from the innermost scope
/// (the "back") to the root. `atoms` is the node's text, e.g. `source.php`.
pub trait NodeAccess {
    type Node: Copy;
    fn parent(&self, node: Self::Node) -> Option<Self::Node>;
    fn atoms(&self, node: Self::Node) -> &[u8];
}

/// Byte at `i` as seen through a C string: 0 past the end (or at an embedded NUL).
#[inline]
fn at(s: &[u8], i: usize) -> u8 {
    if i < s.len() { s[i] } else { 0 }
}

/// C++ `prefix_match` over NUL-terminated strings.
fn prefix_match(lhs: &[u8], rhs: &[u8]) -> bool {
    let (mut i, mut j) = (0, 0);
    while at(lhs, i) != 0 && at(rhs, j) != 0 {
        if at(lhs, i) == at(rhs, j) {
            i += 1;
            j += 1;
        } else if at(lhs, i) == b'*' {
            i += 1;
            while at(rhs, j) != 0 && at(rhs, j) != b'.' {
                j += 1;
            }
        } else {
            return false;
        }
    }
    at(lhs, i) == 0 && (at(rhs, j) == 0 || at(rhs, j) == b'.')
}

#[inline]
fn is_auxiliary_scope(atoms: &[u8]) -> bool {
    // strncmp on c_str(): compares up to an embedded NUL, same as `at`.
    let starts = |p: &[u8]| p.iter().enumerate().all(|(i, &c)| at(atoms, i) == c);
    starts(b"attr.") || starts(b"dyn.")
}

#[inline]
fn number_of_atoms(atoms: &[u8]) -> usize {
    atoms.iter().filter(|&&c| c == b'.').count() + 1
}

/// C++ `std::max(a, b)`: returns `a` unless `a < b`.
#[inline]
fn cxx_max(a: f64, b: f64) -> f64 {
    if a < b { b } else { a }
}

impl Path {
    fn does_match<A: NodeAccess>(&self, a: &A, scope: Option<A::Node>, rank: Option<&mut f64>) -> bool {
        let want_rank = rank.is_some();
        let n = self.scopes.len();

        // `sel` counts selector scopes consumed from the back; `n` is C++'s `rend()`.
        let mut node = scope;
        let mut sel = 0usize;
        let mut score = 0.0f64;

        let mut bt_node: Option<A::Node> = None;
        let mut bt_selector = n;
        let mut bt_score = 0.0f64;

        let mut power = 0.0f64;

        if self.anchor_to_eol {
            while let Some(nd) = node {
                let atoms = a.atoms(nd);
                if !is_auxiliary_scope(atoms) {
                    break;
                }
                if want_rank {
                    power += number_of_atoms(atoms) as f64;
                }
                node = a.parent(nd);
            }
            bt_selector = sel;
        }

        while let Some(nd) = node {
            if sel == n {
                break;
            }
            let atoms = a.atoms(nd);
            if want_rank {
                power += number_of_atoms(atoms) as f64;
            }

            let s = &self.scopes[n - 1 - sel];
            let mut cur = nd;

            let is_redundant_non_bol_match = self.anchor_to_bol && a.parent(nd).is_some() && sel + 1 == n;
            if !is_redundant_non_bol_match && prefix_match(&s.atoms, atoms) {
                if s.anchor_to_previous {
                    if bt_selector == n {
                        bt_node = Some(nd);
                        bt_selector = sel;
                        bt_score = score;
                    }
                } else if bt_selector != n {
                    bt_selector = n;
                }

                if want_rank {
                    let mut len = number_of_atoms(&s.atoms);
                    while len != 0 {
                        len -= 1;
                        score += 1.0 / (power - len as f64).exp2();
                    }
                }

                sel += 1;
            } else if bt_selector != n {
                match bt_node {
                    None => break,
                    Some(b) => {
                        cur = b;
                        sel = bt_selector;
                        score = bt_score;
                        bt_selector = n;
                    }
                }
            }

            node = a.parent(cur);
        }

        if let Some(rank) = rank {
            *rank = if sel == n { score } else { 0.0 };
        }

        sel == n
    }
}

impl Any {
    fn does_match<A: NodeAccess>(&self, a: &A, lhs: Option<A::Node>, rhs: Option<A::Node>, rank: Option<&mut f64>) -> bool {
        match self {
            Any::Path(p) => p.does_match(a, rhs, rank),
            Any::Group(s) => s.does_match_impl(a, lhs, rhs, rank),
            Any::Filter { side, selector } => {
                if *side == b'B' {
                    if let Some(rank) = rank {
                        let (mut r1, mut r2) = (0.0, 0.0);
                        if selector.does_match(a, lhs, lhs, Some(&mut r1)) && selector.does_match(a, rhs, rhs, Some(&mut r2)) {
                            *rank = cxx_max(r1, r2);
                            return true;
                        }
                        return false;
                    }
                }
                match side {
                    b'L' => selector.does_match(a, lhs, lhs, rank),
                    b'R' => selector.does_match(a, rhs, rhs, rank),
                    b'B' => selector.does_match(a, lhs, lhs, None) && selector.does_match(a, rhs, rhs, None),
                    _ => false,
                }
            }
        }
    }
}

#[inline]
fn apply(op: u8, res: bool, local: bool) -> bool {
    match op {
        OP_NONE => local,
        OP_OR => res || local,
        OP_AND => res && local,
        OP_MINUS => res && !local,
        _ => res, // unreachable after parsing; C++ switch has no default either
    }
}

impl Composite {
    fn does_match<A: NodeAccess>(&self, a: &A, lhs: Option<A::Node>, rhs: Option<A::Node>, rank: Option<&mut f64>) -> bool {
        let mut res = false;
        if let Some(rank) = rank {
            let (mut r, mut sum) = (0.0, 0.0);
            for expr in &self.expressions {
                let mut local = expr.selector.does_match(a, lhs, rhs, Some(&mut r));
                if local {
                    sum = cxx_max(r, sum);
                }
                if expr.negate {
                    local = !local;
                }
                res = apply(expr.op, res, local);
            }
            if res {
                *rank = sum;
            }
            return res;
        }

        for expr in &self.expressions {
            let op = expr.op;
            if (res && op == OP_OR) || (!res && op == OP_AND) || (!res && op == OP_MINUS) {
                continue;
            }
            let mut local = expr.selector.does_match(a, lhs, rhs, None);
            if expr.negate {
                local = !local;
            }
            res = apply(op, res, local);
        }
        res
    }
}

impl Selector {
    fn does_match_impl<A: NodeAccess>(&self, a: &A, lhs: Option<A::Node>, rhs: Option<A::Node>, rank: Option<&mut f64>) -> bool {
        if let Some(rank) = rank {
            let (mut res, mut sum) = (false, 0.0);
            let mut r = 0.0;
            for composite in &self.composites {
                if composite.does_match(a, lhs, rhs, Some(&mut r)) {
                    sum = cxx_max(r, sum);
                    res = true;
                }
            }
            if res {
                *rank = sum;
            }
            return res;
        }
        self.composites.iter().any(|c| c.does_match(a, lhs, rhs, None))
    }

    /// Equivalent of C++ `types::selector_t::does_match(lhs, rhs, &rank)`.
    /// The wildcard check and the `optional<double>` wrapping stay in C++.
    pub fn does_match<A: NodeAccess>(&self, a: &A, lhs: Option<A::Node>, rhs: Option<A::Node>, rank: Option<&mut f64>) -> bool {
        self.does_match_impl(a, lhs, rhs, rank)
    }
}

// =========
// = Tests =
// =========

#[cfg(test)]
mod tests {
    use super::*;

    /// A scope path stored root-first; node `i`'s parent is `i - 1`.
    struct VecScope(Vec<Vec<u8>>);

    impl VecScope {
        fn new(s: &str) -> Self {
            VecScope(s.split(' ').filter(|a| !a.is_empty()).map(|a| a.as_bytes().to_vec()).collect())
        }
        fn top(&self) -> Option<usize> {
            self.0.len().checked_sub(1)
        }
    }

    impl NodeAccess for VecScope {
        type Node = usize;
        fn parent(&self, n: usize) -> Option<usize> {
            n.checked_sub(1)
        }
        fn atoms(&self, n: usize) -> &[u8] {
            &self.0[n]
        }
    }

    fn rank(sel: &str, scope: &str) -> Option<f64> {
        let s = VecScope::new(scope);
        let mut r = 1.0;
        Selector::parse(sel.as_bytes()).does_match(&s, s.top(), s.top(), Some(&mut r)).then_some(r)
    }

    fn m(sel: &str, scope: &str) -> bool {
        rank(sel, scope).is_some()
    }

    fn rt(sel: &str) -> String {
        String::from_utf8(Selector::parse(sel.as_bytes()).to_bytes()).unwrap()
    }

    #[test]
    fn round_trip() {
        assert_eq!(rt("foo bar"), "foo bar");
        assert_eq!(rt("foo > bar"), "foo > bar");
        assert_eq!(rt("^ foo $"), "^ foo $");
        assert_eq!(rt("a, b | c & d - e"), "a, b | c & d - e");
        assert_eq!(rt("L:(a b) - R:c"), "L:(a b) - R:c");
        assert_eq!(rt("-foo"), "-foo");
        assert_eq!(rt(""), "");
        assert_eq!(rt("  text.html.* markup "), "text.html.* markup");
    }

    #[test]
    fn child_and_anchor() {
        assert!(m("foo fud", "foo bar fud"));
        assert!(!m("foo > fud", "foo bar fud"));
        assert!(m("foo > foo > fud", "foo foo fud fud"));
        assert!(m("foo > foo fud > fud", "foo foo bar fud fud"));
        assert!(m("^ foo > bar", "foo bar foo"));
        assert!(!m("foo > bar $", "foo bar foo"));
        assert!(!m("^ foo > bar > baz", "foo foo bar baz foo bar baz"));
        assert!(m("foo bar$", "foo bar dyn.selection"));
        assert!(!m("foo bar dyn$", "foo bar dyn.selection"));
        assert!(!m("^ foo > bar > baz $", "foo bar baz foo bar baz"));
    }

    #[test]
    fn ranks_decrease() {
        let scope = "text.html.markdown meta.paragraph.markdown markup.bold.markdown";
        let sels = [
            "text.* markup.bold", "text markup.bold", "markup.bold", "text.html meta.*.markdown markup",
            "text.html meta.* markup", "text.html * markup", "text.html markup", "text markup", "markup",
            "text.html", "text",
        ];
        let mut last = 1.0;
        for s in sels {
            let r = rank(s, scope).expect(s);
            assert!(r < last, "{s}: {r} !< {last}");
            last = r;
        }
    }

    #[test]
    fn high_bytes_are_atom_chars() {
        assert_eq!(rt("café.x"), "café.x");
        assert!(m("café", "café.x"));
    }
}
