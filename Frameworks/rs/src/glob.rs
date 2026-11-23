//! Globs: converts a glob to the regular expression that implements it, and
//! expands braces (`a{b,c}` → `ab`, `ac`). Used for file type globs, folder
//! search, the file browser’s include/exclude rules and `.tm_properties`
//! sections.
//!
//! ```text
//! \«char»           Literal «char»
//! ?                 Match one character
//! *                 Match zero or more characters
//! **                Match zero or more path components
//! {«a»,«b»,«c»}     Match «a» or «b» or «c»
//! [«a»«b»«c»]       Match an «a», «b» or «c» character
//! [«a»-«b»]         Match one character in the range «a»-«b»
//! [^«a»-«b»]        Match one character not in the range «a»-«b»
//! «a»!«b» / «a»~«b» Match «a» AND NOT «b» («a» can be empty)
//! ```
//!
//! A port of Frameworks/regexp/src/parse_glob.cc that produces the same
//! output byte for byte, including for malformed globs. One oddity is kept on
//! purpose: the original tested characters with `strchr`, which also matches
//! the terminating NUL, so a NUL byte in a glob acts as every delimiter.

enum Node {
    Text(Vec<u8>),
    CharClass(Vec<u8>),
    Optional,
    /// `{a,b}`: the alternatives, in order. An alternative may be empty.
    Group(Vec<Vec<Node>>),
    Any,
    /// `**`, with the `/` that followed it, if any.
    AnyRecursive(&'static [u8]),
}

type Seq = Vec<Node>;

/// `strchr(set, ch) != NULL`
#[inline]
fn in_set(set: &[u8], ch: u8) -> bool {
    ch == 0 || set.contains(&ch)
}

/// Length of the UTF-8 sequence that `ch` starts (1 for anything that is not a lead byte).
fn utf8_length(ch: u8) -> usize {
    let mut len = 1;
    if ch & 0xC0 == 0xC0 {
        while len < 6 && ch & (1 << (7 - len)) != 0 {
            len += 1;
        }
    }
    len
}

#[derive(Clone, Copy, PartialEq)]
enum Flavor {
    /// Full glob syntax
    Glob,
    /// Only `{…}` and backslash escapes of `\ { , }`
    Braces,
}

struct Parser<'a> {
    src: &'a [u8],
    it: usize,
    last: usize,
    flavor: Flavor,
    /// Sequences completed by a top-level `!` or `~`; the first one is what must match
    parts: Vec<Seq>,
    /// False for a glob with so many braces that parsing groups recursively
    /// could exhaust the stack; its braces are then all taken literally.
    groups_enabled: bool,
    /// Positions where a “{” turned out not to start a group. Whether it does
    /// depends only on the text that follows, so there is no point trying
    /// again: without this, a run of unbalanced braces takes exponential time
    /// (as it does in the original).
    not_a_group: Vec<bool>,
}

const MAX_BRACES: usize = 256;

impl<'a> Parser<'a> {
    fn new(src: &'a [u8], flavor: Flavor) -> Self {
        let groups_enabled = src.iter().filter(|&&ch| ch == b'{').count() <= MAX_BRACES;
        Parser { src, it: 0, last: src.len(), flavor, parts: Vec::new(), groups_enabled, not_a_group: vec![false; src.len()] }
    }

    fn peek(&self) -> Option<u8> {
        if self.it < self.last { Some(self.src[self.it]) } else { None }
    }

    fn parse_char(&mut self, set: &[u8]) -> bool {
        match self.peek() {
            Some(ch) if in_set(set, ch) => {
                self.it += 1;
                true
            }
            _ => false,
        }
    }

    fn parse_until(&mut self, stop: &[u8]) -> Option<Vec<u8>> {
        let backtrack = self.it;
        let mut res = Vec::new();
        while self.it < self.last && !in_set(stop, self.src[self.it]) {
            if self.src[self.it] == b'\\' && self.it + 1 < self.last && (self.src[self.it + 1] == b'\\' || in_set(stop, self.src[self.it + 1])) {
                self.it += 1;
            }
            res.push(self.src[self.it]);
            self.it += 1;
        }
        if self.parse_char(stop) {
            Some(res)
        } else {
            self.it = backtrack;
            None
        }
    }

    fn parse_text(&mut self, stop: &[u8], seq: &mut Seq) -> bool {
        let backtrack = self.it;
        if self.it < self.last {
            self.it += 1;
            while self.it < self.last && !in_set(stop, self.src[self.it]) {
                self.it += 1;
            }
            seq.push(Node::Text(self.src[backtrack..self.it].to_vec()));
            return true;
        }
        false
    }

    fn parse_escape(&mut self, seq: &mut Seq) -> bool {
        let backtrack = self.it;
        match self.flavor {
            Flavor::Braces => {
                if self.parse_char(b"\\") && self.it < self.last && in_set(b"\\{,}", self.src[self.it]) {
                    seq.push(Node::Text(vec![self.src[self.it]]));
                    self.it += 1;
                    return true;
                }
            }
            Flavor::Glob => {
                if self.parse_char(b"\\") && self.it < self.last {
                    let ch = self.src[self.it];
                    if in_set(b"trn", ch) {
                        self.it += 1;
                        let text: &[u8] = match ch {
                            b't' => b"\t",
                            b'r' => b"\r",
                            b'n' => b"\n",
                            _ => b"", // a NUL: not an escape
                        };
                        if !text.is_empty() {
                            seq.push(Node::Text(text.to_vec()));
                            return true;
                        }
                    } else {
                        // the escaped character is taken literally, all of its bytes
                        let mut len = utf8_length(ch);
                        while self.it < self.last {
                            self.it += 1;
                            len -= 1;
                            if len == 0 {
                                seq.push(Node::Text(self.src[backtrack + 1..self.it].to_vec()));
                                return true;
                            }
                        }
                    }
                }
            }
        }
        self.it = backtrack;
        false
    }

    fn parse_brace_expansion(&mut self, seq: &mut Seq) -> bool {
        let backtrack = self.it;
        if !self.groups_enabled || self.not_a_group.get(backtrack).copied().unwrap_or(true) {
            return false;
        }
        if self.parse_char(b"{") {
            let mut has_comma = false;
            let mut alternatives: Vec<Seq> = Vec::new();
            loop {
                alternatives.push(self.parse_seq(b",}", true));
                if self.parse_char(b"}") {
                    if has_comma {
                        seq.push(Node::Group(alternatives));
                        return true;
                    }

                    // No comma, so not a group: take the text from “{” to “}” literally
                    let (end, old_last) = (self.it, self.last);
                    self.it = backtrack;
                    self.last = end;
                    while self.it < self.last && (self.parse_escape(seq) || self.parse_text(b"\\", seq)) {}
                    self.last = old_last;
                    return true;
                } else if self.parse_char(b",") {
                    has_comma = true;
                } else {
                    break;
                }
            }
        }
        self.not_a_group[backtrack] = true;
        self.it = backtrack;
        false
    }

    fn parse_seq(&mut self, stop: &[u8], in_braces: bool) -> Seq {
        let mut seq = Seq::new();
        while self.it < self.last && !in_set(stop, self.src[self.it]) {
            match self.flavor {
                Flavor::Braces => {
                    let _ = self.parse_escape(&mut seq)
                        || self.parse_brace_expansion(&mut seq)
                        || self.parse_text(if in_braces { b"\\{,}" } else { b"\\{" }, &mut seq);
                }
                Flavor::Glob => {
                    if self.parse_escape(&mut seq) {
                    } else if self.parse_char(b"?") {
                        seq.push(Node::Optional);
                    } else if self.parse_any_recursive(&mut seq) {
                    } else if self.parse_char(b"*") {
                        seq.push(Node::Any);
                    } else if self.parse_brace_expansion(&mut seq) {
                    } else if self.parse_character_class(&mut seq) {
                    } else if !in_braces && self.parse_exclude() {
                        // what follows is a pattern that must *not* match
                        self.parts.push(std::mem::take(&mut seq));
                    } else {
                        self.parse_text(if in_braces { b"\\?*{[,}" } else { b"\\?*{[!~" }, &mut seq);
                    }
                }
            }
        }
        seq
    }

    fn parse_any_recursive(&mut self, seq: &mut Seq) -> bool {
        let backtrack = self.it;
        if self.parse_char(b"*") && self.parse_char(b"*") {
            seq.push(Node::AnyRecursive(if self.parse_char(b"/") { b"/" } else { b"" }));
            return true;
        }
        self.it = backtrack;
        false
    }

    fn parse_character_class(&mut self, seq: &mut Seq) -> bool {
        let backtrack = self.it;
        if self.parse_char(b"[") {
            if let Some(group) = self.parse_until(b"]") {
                seq.push(Node::CharClass(group));
                return true;
            }
        }
        self.it = backtrack;
        false
    }

    fn parse_exclude(&mut self) -> bool {
        let backtrack = self.it;
        if (self.parse_char(b"~") || self.parse_char(b"!")) && self.it < self.last {
            return true;
        }
        self.it = backtrack;
        false
    }
}

fn to_regexp(seq: &[Node], match_dot_files: bool, out: &mut Vec<u8>) {
    for node in seq {
        match node {
            Node::Text(text) => {
                for &ch in text {
                    if in_set(b"[](){}\\|.?*+^$", ch) {
                        out.push(b'\\');
                    }
                    out.push(ch);
                }
            }
            Node::CharClass(text) => {
                out.push(b'[');
                out.extend_from_slice(text);
                out.push(b']');
            }
            Node::Optional => out.push(b'.'),
            Node::Group(alternatives) => {
                out.extend_from_slice(b"(?:");
                for (i, alternative) in alternatives.iter().enumerate() {
                    if i != 0 {
                        out.push(b'|');
                    }
                    to_regexp(alternative, match_dot_files, out);
                }
                out.push(b')');
            }
            Node::Any => out.extend_from_slice(if match_dot_files { b"[^/]*" } else { b"(?:(?!\\.)|(?<!^|/))[^/]*" }),
            Node::AnyRecursive(slash) => {
                out.extend_from_slice(if match_dot_files { b"(?:.*" } else { b"(?:(?:(?!\\.)|(?<!^|/))[^/]*(?:/(?!\\.)[^/]*)*" });
                out.extend_from_slice(slash);
                out.extend_from_slice(b")?");
            }
        }
    }
}

/// The regular expression for `glob`, or `None` if the glob is empty.
/// Without `match_dot_files`, `*` and `**` do not match a leading period.
pub fn glob_to_regexp(glob: &[u8], match_dot_files: bool) -> Option<Vec<u8>> {
    let mut parser = Parser::new(glob, Flavor::Glob);
    let tail = parser.parse_seq(b"", false);
    let mut parts = std::mem::take(&mut parser.parts);
    parts.push(tail);

    if parts.len() == 1 && parts[0].is_empty() {
        return None;
    }

    let mut includes = Vec::new();
    to_regexp(&parts[0], match_dot_files, &mut includes);

    let mut excludes: Vec<u8> = Vec::new();
    for part in &parts[1..] {
        let mut pattern = Vec::new();
        to_regexp(part, true, &mut pattern);
        if !excludes.is_empty() {
            excludes.push(b'|');
        }
        excludes.extend_from_slice(&pattern);
    }

    let mut res = b"^".to_vec();
    if !excludes.is_empty() {
        res.extend_from_slice(b"(?!(?:.*/)?(?:");
        res.extend_from_slice(&excludes);
        res.extend_from_slice(b")$)");
    }
    if !includes.is_empty() {
        res.extend_from_slice(b"(?:.*/)?(?:");
        res.extend_from_slice(&includes);
        res.push(b')');
    } else {
        res.extend_from_slice(b".*");
    }
    res.push(b'$');
    Some(res)
}

/// Brace expansion can double the output with every group; beyond this the
/// expansion stops growing rather than exhaust memory on a hostile glob.
const MAX_EXPANSIONS: usize = 1 << 16;

fn expand_seq(seq: &[Node], strings: &mut Vec<Vec<u8>>) {
    for node in seq {
        match node {
            Node::Text(text) => strings.iter_mut().for_each(|s| s.extend_from_slice(text)),
            Node::Group(alternatives) => expand_alternatives(alternatives, strings),
            _ => {}
        }
    }
}

/// The last alternative is expanded on a copy and appended after the others.
/// As in the original, an empty alternative that is not the first is dropped.
fn expand_alternatives(alternatives: &[Seq], strings: &mut Vec<Vec<u8>>) {
    let Some((current, earlier)) = alternatives.split_last() else { return };
    if !earlier.is_empty() && !current.is_empty() {
        let mut tmp = if strings.len() < MAX_EXPANSIONS { strings.clone() } else { Vec::new() };
        expand_seq(current, &mut tmp);
        expand_alternatives(earlier, strings);
        strings.append(&mut tmp);
    } else {
        expand_alternatives(earlier, strings);
        expand_seq(current, strings);
    }
}

/// `a{b,c}d` → `abd`, `acd`. A string without braces expands to itself.
pub fn expand_braces(glob: &[u8]) -> Vec<Vec<u8>> {
    let mut parser = Parser::new(glob, Flavor::Braces);
    let seq = parser.parse_seq(b"", false);
    let mut res = vec![Vec::new()];
    expand_seq(&seq, &mut res);
    res
}

#[cfg(test)]
mod tests {
    use super::*;

    fn re(glob: &str, dot: bool) -> Option<String> {
        glob_to_regexp(glob.as_bytes(), dot).map(|v| String::from_utf8(v).unwrap())
    }

    fn braces(glob: &str) -> Vec<String> {
        expand_braces(glob.as_bytes()).into_iter().map(|v| String::from_utf8(v).unwrap()).collect()
    }

    #[test]
    fn regexps() {
        assert_eq!(re("", false), None);
        assert_eq!(re("*.txt", true).unwrap(), "^(?:.*/)?(?:[^/]*\\.txt)$");
        assert_eq!(re("*.txt", false).unwrap(), "^(?:.*/)?(?:(?:(?!\\.)|(?<!^|/))[^/]*\\.txt)$");
        assert_eq!(re("*.{cc,h}", true).unwrap(), "^(?:.*/)?(?:[^/]*\\.(?:cc|h))$");
        assert_eq!(re("{a}", true).unwrap(), "^(?:.*/)?(?:\\{a\\})$");
        assert_eq!(re("[a-z]?", true).unwrap(), "^(?:.*/)?(?:[a-z].)$");
        assert_eq!(re("**/x", true).unwrap(), "^(?:.*/)?(?:(?:.*/)?x)$");
        assert_eq!(re("*~*.o", true).unwrap(), "^(?!(?:.*/)?(?:[^/]*\\.o)$)(?:.*/)?(?:[^/]*)$");
        assert_eq!(re("!*.o", true).unwrap(), "^(?!(?:.*/)?(?:[^/]*\\.o)$).*$");
        assert_eq!(re("\\*", true).unwrap(), "^(?:.*/)?(?:\\*)$");
    }

    #[test]
    fn brace_expansion() {
        assert_eq!(braces(""), [""]);
        assert_eq!(braces("plain"), ["plain"]);
        assert_eq!(braces("a{b,c}d"), ["abd", "acd"]);
        assert_eq!(braces("{a,b}{1,2}"), ["a1", "b1", "a2", "b2"]);
        assert_eq!(braces("{a}"), ["{a}"]);
        assert_eq!(braces("x{a,{b,c}}"), ["xa", "xb", "xc"]);
        assert_eq!(braces("\\{a,b\\}"), ["{a,b}"]);
    }

    #[test]
    fn hostile_input_terminates() {
        let deep = "{".repeat(5000);
        assert!(glob_to_regexp(deep.as_bytes(), false).is_some());
        assert_eq!(expand_braces(deep.as_bytes()), [deep.as_bytes()]);
        let unbalanced = "{".repeat(200);
        assert_eq!(expand_braces(unbalanced.as_bytes()), [unbalanced.as_bytes()]);
        let nested = format!("{}x{}", "{a,".repeat(200), "}".repeat(200));
        assert!(glob_to_regexp(nested.as_bytes(), false).is_some());
        let wide = "{a,b}".repeat(40);
        assert!(expand_braces(wide.as_bytes()).len() <= MAX_EXPANSIONS * 2);
        assert!(glob_to_regexp(b"a\0b{c,\0", true).is_some());
        assert!(glob_to_regexp(b"\\", true).is_some());
        assert!(glob_to_regexp(b"[\\", true).is_some());
        assert!(glob_to_regexp(b"\\\xFF", true).is_some());
    }
}
