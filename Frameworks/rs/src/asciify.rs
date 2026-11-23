//! Reduces text to plain ASCII, for the `asciify` format-string transform
//! (`${TM_FILENAME/…/asciify}`), which bundles use to derive identifiers and
//! file names.
//!
//! The caller has already stripped diacritics with CoreFoundation, so what is
//! left are letters that do not decompose (æ, ø, ß, …) and punctuation. This
//! used to go through `iconv(…, "ASCII//TRANSLIT")`, whose result depends on
//! the macOS version: current systems drop characters and emit control codes
//! for dashes. The table here gives the same answer everywhere.

/// ASCII replacement for `ch`, or `None` if there is no sensible one.
fn replacement(ch: char) -> Option<&'static str> {
    Some(match ch {
        // Letters without a canonical decomposition
        'Æ' => "AE", 'æ' => "ae", 'Œ' => "OE", 'œ' => "oe",
        'Ø' => "O", 'ø' => "o", 'Đ' | 'Ð' => "D", 'đ' | 'ð' => "d",
        'Þ' => "TH", 'þ' => "th", 'ß' => "ss", 'ẞ' => "SS",
        'Ł' => "L", 'ł' => "l", 'Ħ' => "H", 'ħ' => "h",
        'ı' => "i", 'ȷ' => "j", 'ĸ' => "k", 'Ŋ' => "NG", 'ŋ' => "ng",
        'ſ' => "s", 'Ŧ' => "T", 'ŧ' => "t",
        'ﬀ' => "ff", 'ﬁ' => "fi", 'ﬂ' => "fl", 'ﬃ' => "ffi", 'ﬄ' => "ffl",

        // Quotes
        '‘' | '’' | '‚' | '‛' | '′' | '‹' | '›' => "'",
        '“' | '”' | '„' | '‟' | '″' => "\"",
        '«' => "<<", '»' => ">>",

        // Dashes and spaces
        '‐' | '‑' | '‒' | '–' | '—' | '―' | '−' => "-",
        '\u{00A0}' | '\u{2000}'..='\u{200A}' | '\u{202F}' | '\u{205F}' | '\u{3000}' => " ",
        '\u{00AD}' | '\u{200B}'..='\u{200D}' | '\u{2060}' | '\u{FEFF}' => "",

        // Combining marks the caller did not remove
        '\u{0300}'..='\u{036F}' => "",

        // Punctuation and symbols
        '…' => "...", '‥' => "..", '·' | '•' | '∙' => "*",
        '¡' => "!", '¿' => "?", '‼' => "!!", '⁄' | '∕' | '÷' => "/",
        '×' => "x", '±' => "+/-", '≤' => "<=", '≥' => ">=", '≠' => "!=", '≈' => "~",
        '←' => "<-", '→' => "->", '⇐' => "<=", '⇒' => "=>",
        '©' => "(c)", '®' => "(R)", '™' => "(TM)", '℠' => "(SM)",
        '°' => "deg", 'µ' | 'μ' => "u", '№' => "No",
        '¼' => "1/4", '½' => "1/2", '¾' => "3/4",
        '¹' => "1", '²' => "2", '³' => "3",
        '€' => "EUR", '£' => "GBP", '¥' => "JPY", '¢' => "c",
        '§' => "S", '¶' => "P", '†' => "+", '‡' => "++",
        _ => return None,
    })
}

/// Returns `src` as ASCII. Characters without a replacement, and bytes that
/// are not valid UTF-8, become `?` (one per character or invalid sequence).
pub fn asciify(src: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(src.len());
    for chunk in src.utf8_chunks() {
        for ch in chunk.valid().chars() {
            if ch.is_ascii() {
                out.push(ch as u8);
            } else {
                out.extend_from_slice(replacement(ch).unwrap_or("?").as_bytes());
            }
        }
        if !chunk.invalid().is_empty() {
            out.push(b'?');
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::asciify;

    fn a(s: &str) -> String {
        String::from_utf8(asciify(s.as_bytes())).unwrap()
    }

    #[test]
    fn ascii_is_untouched() {
        assert_eq!(a(""), "");
        assert_eq!(a("plain text\t\n ~"), "plain text\t\n ~");
    }

    #[test]
    fn letters_and_punctuation() {
        assert_eq!(a("AEblegrød…"), "AEblegrod...");
        assert_eq!(a("æbleGRØD"), "aebleGROD");
        assert_eq!(a("Straße"), "Strasse");
        assert_eq!(a("“quotes” — dash"), "\"quotes\" - dash");
        assert_eq!(a("it’s 5 × 3 → 15"), "it's 5 x 3 -> 15");
        assert_eq!(a("a\u{00A0}b\u{200B}c"), "a bc");
        assert_eq!(a("e\u{0301}"), "e");
    }

    #[test]
    fn unknown_and_invalid_become_question_marks() {
        assert_eq!(a("日本"), "??");
        assert_eq!(a("😀!"), "?!");
        assert_eq!(asciify(b"a\xFFb"), b"a?b");
        assert_eq!(asciify(b"a\xE2\x80"), b"a?");
        assert_eq!(asciify(b"\xC3"), b"?");
    }

    #[test]
    fn output_is_always_ascii() {
        let mut all = Vec::new();
        for b in 0u8..=255 {
            all.extend_from_slice(&[b, b'x', b]);
        }
        assert!(asciify(&all).is_ascii());
        let text: String = (0x80u32..0x3000).filter_map(char::from_u32).collect();
        assert!(asciify(text.as_bytes()).is_ascii());
    }
}
