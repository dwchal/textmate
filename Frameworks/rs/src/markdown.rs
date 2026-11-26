//! Markdown structure for the editor’s formatted mode.
//!
//! The editor keeps showing the Markdown source, but when formatted mode is on
//! it draws headings, emphasis, code and links styled and hides their markers
//! (`#`, `**`, backticks, `[…](url)`) unless the caret is in that element.
//! This module finds those elements: it returns byte ranges of the source and
//! never changes the text, so switching modes cannot alter the document.
//!
//! Parsing is CommonMark with the GitHub extensions (pulldown-cmark).

use pulldown_cmark::{Event, HeadingLevel, Options, Parser, Tag};

/// What to do with `from..to`. Values are part of the C ABI (`tm_rs.h`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u32)]
pub enum Kind {
    Heading1 = 1,
    Heading2 = 2,
    Heading3 = 3,
    Heading4 = 4,
    Heading5 = 5,
    Heading6 = 6,
    Bold = 7,
    Italic = 8,
    Strikethrough = 9,
    Code = 10,
    Link = 11,
    /// A marker to hide while the caret is outside `owner`.
    Conceal = 20,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Span {
    pub kind: Kind,
    pub from: usize,
    pub to: usize,
    /// The whole element this span belongs to, markers included.
    pub owner: (usize, usize),
}

/// Documents larger than this are left unformatted.
pub const MAX_LEN: usize = 2 * 1024 * 1024;

struct Frame {
    tag: FrameTag,
    range: (usize, usize),
    /// Extent of the element’s children, i.e. the text between its markers.
    inner: Option<(usize, usize)>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum FrameTag {
    Heading(Kind),
    Emphasis,
    Strong,
    Strikethrough,
    Link,
    Other,
}

fn heading_kind(level: HeadingLevel) -> Kind {
    match level {
        HeadingLevel::H1 => Kind::Heading1,
        HeadingLevel::H2 => Kind::Heading2,
        HeadingLevel::H3 => Kind::Heading3,
        HeadingLevel::H4 => Kind::Heading4,
        HeadingLevel::H5 => Kind::Heading5,
        HeadingLevel::H6 => Kind::Heading6,
    }
}

struct Collector<'a> {
    src: &'a [u8],
    out: Vec<Span>,
}

impl Collector<'_> {
    fn at(&self, i: usize) -> u8 {
        self.src.get(i).copied().unwrap_or(0)
    }

    fn run_length(&self, from: usize, to: usize, ch: u8) -> usize {
        (from..to).take_while(|&i| self.at(i) == ch).count()
    }

    fn push(&mut self, kind: Kind, from: usize, to: usize, owner: (usize, usize)) {
        if from < to && to <= self.src.len() {
            self.out.push(Span { kind, from, to, owner });
        }
    }

    /// An element with `n` marker bytes on each side.
    fn delimited(&mut self, kind: Kind, (s, e): (usize, usize), n: usize) {
        if n == 0 || e < s + 2 * n + 1 {
            return;
        }
        self.push(Kind::Conceal, s, s + n, (s, e));
        self.push(kind, s + n, e - n, (s, e));
        self.push(Kind::Conceal, e - n, e, (s, e));
    }

    fn heading(&mut self, kind: Kind, (s, mut e): (usize, usize)) {
        while e > s && matches!(self.at(e - 1), b'\n' | b'\r' | b' ' | b'\t') {
            e -= 1;
        }
        let mut i = s;
        while i < e && i < s + 3 && self.at(i) == b' ' {
            i += 1;
        }
        let hashes = self.run_length(i, e, b'#');
        if hashes == 0 {
            // Setext heading (underlined with === or ---): style the first line only
            let eol = (s..e).find(|&j| self.at(j) == b'\n').unwrap_or(e);
            self.push(kind, s, eol, (s, e));
            return;
        }
        let mut text = i + hashes;
        while text < e && matches!(self.at(text), b' ' | b'\t') {
            text += 1;
        }
        if text < e {
            self.push(Kind::Conceal, s, text, (s, e));
            self.push(kind, text, e, (s, e));
        }
    }

    fn link(&mut self, (s, e): (usize, usize), inner: Option<(usize, usize)>) {
        match self.at(s) {
            b'[' => {
                let Some((inner_s, inner_e)) = inner else { return };
                if inner_s == s + 1 && inner_e < e && self.at(inner_e) == b']' {
                    self.push(Kind::Conceal, s, inner_s, (s, e));
                    self.push(Kind::Link, inner_s, inner_e, (s, e));
                    self.push(Kind::Conceal, inner_e, e, (s, e));
                }
            }
            b'<' if e >= s + 3 && self.at(e - 1) == b'>' => self.delimited(Kind::Link, (s, e), 1),
            _ => {}
        }
    }

    fn close(&mut self, frame: &Frame) {
        let (s, e) = frame.range;
        match frame.tag {
            FrameTag::Heading(kind) => self.heading(kind, frame.range),
            FrameTag::Emphasis => {
                let ch = self.at(s);
                if (ch == b'*' || ch == b'_') && e > s && self.at(e - 1) == ch {
                    self.delimited(Kind::Italic, frame.range, 1);
                }
            }
            FrameTag::Strong => {
                let ch = self.at(s);
                if (ch == b'*' || ch == b'_') && e >= s + 4 && self.at(s + 1) == ch && self.at(e - 1) == ch && self.at(e - 2) == ch {
                    self.delimited(Kind::Bold, frame.range, 2);
                }
            }
            FrameTag::Strikethrough => {
                let n = self.run_length(s, e, b'~').min(2);
                if n != 0 && e >= s + 2 * n && (e - n..e).all(|i| self.at(i) == b'~') {
                    self.delimited(Kind::Strikethrough, frame.range, n);
                }
            }
            FrameTag::Link => self.link(frame.range, frame.inner),
            FrameTag::Other => {}
        }
    }

    fn code(&mut self, (s, e): (usize, usize)) {
        let n = self.run_length(s, e, b'`');
        if n != 0 && e >= s + 2 * n && (e - n..e).all(|i| self.at(i) == b'`') {
            self.delimited(Kind::Code, (s, e), n);
        }
    }
}

/// The styled and hideable ranges of `src`, ordered by `from` (a span comes
/// before the spans nested inside it). Returns nothing for text that is not
/// valid UTF-8 or is larger than [`MAX_LEN`].
pub fn spans(src: &[u8]) -> Vec<Span> {
    if src.len() > MAX_LEN {
        return Vec::new();
    }
    let Ok(text) = std::str::from_utf8(src) else { return Vec::new() };

    let options = Options::ENABLE_TABLES
        | Options::ENABLE_STRIKETHROUGH
        | Options::ENABLE_TASKLISTS
        | Options::ENABLE_FOOTNOTES
        | Options::ENABLE_YAML_STYLE_METADATA_BLOCKS;

    let mut collector = Collector { src, out: Vec::new() };
    let mut stack: Vec<Frame> = Vec::new();

    for (event, range) in Parser::new_ext(text, options).into_offset_iter() {
        let range = (range.start, range.end.min(src.len()));
        if range.0 > range.1 {
            continue;
        }

        let is_end = matches!(event, Event::End(_));
        if !is_end {
            // Everything inside an element extends what lies between its markers
            if let Some(parent) = stack.last_mut() {
                parent.inner = Some(match parent.inner {
                    None => range,
                    Some((s, e)) => (s.min(range.0), e.max(range.1)),
                });
            }
        }

        match event {
            Event::Start(tag) => {
                let tag = match tag {
                    Tag::Heading { level, .. } => FrameTag::Heading(heading_kind(level)),
                    Tag::Emphasis => FrameTag::Emphasis,
                    Tag::Strong => FrameTag::Strong,
                    Tag::Strikethrough => FrameTag::Strikethrough,
                    Tag::Link { .. } => FrameTag::Link,
                    _ => FrameTag::Other,
                };
                stack.push(Frame { tag, range, inner: None });
            }
            Event::End(_) => {
                if let Some(frame) = stack.pop() {
                    collector.close(&frame);
                }
            }
            Event::Code(_) => collector.code(range),
            _ => {}
        }
    }

    let mut out = collector.out;
    out.sort_by(|a, b| a.from.cmp(&b.from).then(b.to.cmp(&a.to)));
    out
}

#[cfg(test)]
mod tests {
    use super::{spans, Kind, Span};

    /// Renders the result compactly: `kind:text` for styles, `-text-` for hidden markers.
    fn describe(src: &str) -> Vec<String> {
        spans(src.as_bytes())
            .iter()
            .map(|span| {
                let text = &src[span.from..span.to];
                match span.kind {
                    Kind::Conceal => format!("-{text}-"),
                    kind => format!("{kind:?}:{text}"),
                }
            })
            .collect()
    }

    #[test]
    fn headings() {
        assert_eq!(describe("# Title\n"), ["-# -", "Heading1:Title"]);
        assert_eq!(describe("###   Deep  \ntext\n"), ["-###   -", "Heading3:Deep"]);
        assert_eq!(describe("#\n"), Vec::<String>::new());
        assert_eq!(describe("#hashtag\n"), Vec::<String>::new());
        assert_eq!(describe("Title\n=====\n"), ["Heading1:Title"]);
    }

    #[test]
    fn emphasis() {
        assert_eq!(describe("a **b** c"), ["-**-", "Bold:b", "-**-"]);
        assert_eq!(describe("a *b* c"), ["-*-", "Italic:b", "-*-"]);
        assert_eq!(describe("a __b__ _c_"), ["-__-", "Bold:b", "-__-", "-_-", "Italic:c", "-_-"]);
        assert_eq!(describe("~~gone~~"), ["-~~-", "Strikethrough:gone", "-~~-"]);
        assert_eq!(describe("2 * 3 * 4"), Vec::<String>::new());
        assert_eq!(describe("snake_case_name"), Vec::<String>::new());
        assert_eq!(describe("\\*not\\*"), Vec::<String>::new());
    }

    #[test]
    fn nesting() {
        assert_eq!(describe("***x***"), ["-*-", "Italic:**x**", "-**-", "Bold:x", "-**-", "-*-"]);
        assert_eq!(describe("# A *b*\n"), ["-# -", "Heading1:A *b*", "-*-", "Italic:b", "-*-"]);
    }

    #[test]
    fn code() {
        assert_eq!(describe("use `x` here"), ["-`-", "Code:x", "-`-"]);
        assert_eq!(describe("``a ` b``"), ["-``-", "Code:a ` b", "-``-"]);
        assert_eq!(describe("```\n**not bold**\n```\n"), Vec::<String>::new());
        assert_eq!(describe("    **indented code**\n"), Vec::<String>::new());
    }

    #[test]
    fn links() {
        assert_eq!(describe("see [the site](https://example.com) now"), ["-[-", "Link:the site", "-](https://example.com)-"]);
        assert_eq!(describe("[a *b*](u)"), ["-[-", "Link:a *b*", "-*-", "Italic:b", "-*-", "-](u)-"]);
        assert_eq!(describe("<https://example.com>"), ["-<-", "Link:https://example.com", "->-"]);
        assert_eq!(describe("[ref][1]\n\n[1]: https://example.com\n"), ["-[-", "Link:ref", "-][1]-"]);
        assert_eq!(describe("![image](a.png)"), Vec::<String>::new());
        assert_eq!(describe("[not a link]"), Vec::<String>::new());
    }

    #[test]
    fn owners_cover_the_whole_element() {
        let found = spans(b"x **bold** y");
        assert_eq!(found[0], Span { kind: Kind::Conceal, from: 2, to: 4, owner: (2, 10) });
        assert!(found.iter().all(|span| span.owner == (2, 10)));
    }

    #[test]
    fn spans_are_ordered_and_in_bounds() {
        let src = "# H *i*\n\ntext **b `c`** [l](u) ~~s~~\n\n- item *e*\n\n| a | b |\n|---|---|\n| *c* | d |\n";
        let found = spans(src.as_bytes());
        assert!(!found.is_empty());
        for pair in found.windows(2) {
            assert!(pair[0].from <= pair[1].from);
        }
        for span in &found {
            assert!(span.from < span.to && span.to <= src.len());
            assert!(span.owner.0 <= span.from && span.to <= span.owner.1);
            assert!(src.is_char_boundary(span.from) && src.is_char_boundary(span.to));
        }
    }

    #[test]
    fn hidden_markers_never_overlap() {
        let src = "***a*** **b *c* d** `e` [f **g**](h) # not\n\n## H `c` **b**\n";
        let mut hidden: Vec<_> = spans(src.as_bytes()).into_iter().filter(|s| s.kind == Kind::Conceal).collect();
        hidden.sort_by_key(|s| s.from);
        for pair in hidden.windows(2) {
            assert!(pair[0].to <= pair[1].from, "{pair:?}");
        }
    }

    #[test]
    fn odd_input() {
        assert!(spans(b"").is_empty());
        assert!(spans(b"\xFF\xFE **x**").is_empty());
        assert!(spans("é **ü** 日本 `語`".as_bytes()).len() == 6);
        // every prefix of a tricky document, to shake out range arithmetic
        let src = "# *a\n**b `c\n[d](e\n~~f~ ~g~\n<h> `` ` ``\n***\n\\# \\*\n";
        for end in 0..=src.len() {
            if src.is_char_boundary(end) {
                for span in spans(&src.as_bytes()[..end]) {
                    assert!(span.from < span.to && span.to <= end);
                }
            }
        }
    }
}

#[cfg(test)]
mod speed {
    /// The editor analyses the whole document after every edit, so this has to
    /// stay far below a frame for ordinary documents. Run with `--ignored --nocapture`.
    #[test]
    #[ignore]
    fn megabyte() {
        let paragraph = "## Heading *x*\n\nSome **bold** text with `code`, a [link](https://example.com) and ~~more~~ words to fill the line.\n\n- item one\n- item *two*\n\n";
        let doc = paragraph.repeat(1024 * 1024 / paragraph.len());
        let start = std::time::Instant::now();
        let found = super::spans(doc.as_bytes());
        println!("{} bytes, {} spans, {:?}", doc.len(), found.len(), start.elapsed());
    }
}
