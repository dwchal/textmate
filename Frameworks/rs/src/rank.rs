//! Fuzzy matching for the file, symbol and bundle item choosers: how well does
//! `filter` (what the user typed, already lower-cased and without spaces)
//! match `candidate`, and which parts of the candidate does it cover?
//!
//! This is a port of `oak::rank` (Frameworks/text/src/ranker.cc) and gives the
//! same scores bit for bit, with two deliberate exceptions where the C++ was
//! wrong:
//!
//! * A candidate with no “word start” characters (e.g. `.gitignore`) divided
//!   zero by zero and ranked as NaN, which breaks sorting. That term is now
//!   left out.
//! * For long candidates the fallback `filter.size() / candidate.size()` was an
//!   integer division, i.e. 0, which means “no match”: long paths silently
//!   vanished from the chooser. It is now the real ratio.
//!
//! Characters are bytes with C-locale semantics, as in the original: only
//! ASCII letters have case, and bytes ≥ 0x80 are never alphanumeric.

use std::cell::RefCell;

/// Above this many matrix cells the candidate is only given a rough score.
const MAX_CELLS: usize = 8096;

fn is_subset(needle: &[u8], haystack: &[u8]) -> bool {
    let mut n = 0;
    for &ch in haystack {
        if n == needle.len() {
            break;
        }
        if needle[n] == ch || needle[n].to_ascii_uppercase() == ch {
            n += 1;
        }
    }
    n == needle.len()
}

thread_local! {
    static SCRATCH: RefCell<Vec<usize>> = const { RefCell::new(Vec::new()) };
}

struct Matrix<'a> {
    cells: &'a mut [usize],
    m: usize,
}

impl Matrix<'_> {
    /// Out-of-range reads give 0 (“no match here”) instead of panicking.
    #[inline]
    fn get(&self, i: usize, j: usize) -> usize {
        if j < self.m {
            self.cells.get(i * self.m + j).copied().unwrap_or(0)
        } else {
            0
        }
    }

    #[inline]
    fn set(&mut self, i: usize, j: usize, value: usize) {
        if j < self.m {
            if let Some(cell) = self.cells.get_mut(i * self.m + j) {
                *cell = value;
            }
        }
    }
}

fn calculate_rank(lhs: &[u8], rhs: &[u8], mut out: Option<&mut Vec<(usize, usize)>>) -> f64 {
    let n = lhs.len();
    let m = rhs.len();

    SCRATCH.with(|scratch| {
        let mut scratch = scratch.borrow_mut();
        scratch.clear();
        scratch.resize(n * m, 0);
        let mut matrix = Matrix { cells: &mut scratch[..], m };

        let mut first = vec![m; n];
        let mut last = vec![0usize; n];

        // “Capitals” are the characters a filter is most likely aiming for:
        // upper-case letters and the first letter or digit of each word.
        let mut capitals = vec![false; m];
        let mut at_bow = true;
        for (j, &ch) in rhs.iter().enumerate() {
            capitals[j] = at_bow && ch.is_ascii_alphanumeric() || ch.is_ascii_uppercase();
            at_bow = !ch.is_ascii_alphanumeric() && ch != b'\'' && ch != b'.';
        }
        let capital = |j: usize| capitals.get(j).copied().unwrap_or(false);

        // matrix[i][j] = length of the common run ending with lhs[i] == rhs[j]
        for i in 0..n {
            let start = if i == 0 { 0 } else { first[i - 1].wrapping_add(1) };
            let mut j = start;
            while j < m {
                if lhs[i].to_ascii_lowercase() == rhs[j].to_ascii_lowercase() {
                    let run = if i == 0 || j == 0 { 1 } else { matrix.get(i - 1, j - 1) + 1 };
                    matrix.set(i, j, run);
                    first[i] = first[i].min(j);
                    last[i] = last[i].max(j + 1);
                }
                j += 1;
            }
        }

        // Row i-1 cannot use columns at or after the last match of row i
        for i in (1..n).rev() {
            let mut bound = last[i].wrapping_sub(1);
            if bound < last[i - 1] {
                while first[i - 1] < bound && matrix.get(i - 1, bound - 1) == 0 {
                    bound -= 1;
                }
                last[i - 1] = bound;
            }
        }

        // Turn “length of the run ending here” into “length of the run starting here”
        for i in (1..n).rev() {
            for j in first[i]..last[i] {
                let here = matrix.get(i, j);
                if here != 0 && j > 0 && matrix.get(i - 1, j - 1) != 0 {
                    matrix.set(i - 1, j - 1, here);
                }
            }
        }

        for i in 0..n {
            for j in first[i]..last[i] {
                let here = matrix.get(i, j);
                if here > 1 && i + 1 < n && j + 1 < m {
                    matrix.set(i + 1, j + 1, here - 1);
                }
            }
        }

        // Greedy walk of the matrix
        let mut capitals_touched = 0usize; // 0-n
        let mut substrings = 0usize; // 1-n
        let mut prefix_size = 0usize; // 0-m

        let mut i = 0;
        while i < n {
            let mut best_index = 0;
            let mut best_length = 0;
            for j in first[i]..last[i] {
                let run = matrix.get(i, j);
                if run != 0 && capital(j) {
                    best_index = j;
                    best_length = run;
                    capitals_touched += (j..j.saturating_add(run)).filter(|&k| capital(k)).count();
                    break;
                } else if best_length < run {
                    best_index = j;
                    best_length = run;
                }
            }

            if i == 0 {
                prefix_size = best_index;
            }

            let mut len = 0;
            let mut found_capital = false;
            loop {
                i += 1;
                len += 1;
                if i == n {
                    break;
                }

                first[i] = first[i].max(best_index + len);
                if len < best_length && n < 4 {
                    if capital(first[i]) {
                        continue;
                    }
                    let mut j = first[i];
                    while j < last[i] && !found_capital {
                        if matrix.get(i, j) != 0 && capital(j) {
                            found_capital = true;
                        }
                        j += 1;
                    }
                }

                if !(len < best_length && !found_capital) {
                    break;
                }
            }

            if let Some(out) = out.as_deref_mut() {
                out.push((best_index, best_index + len));
            }
            substrings += 1;
        }

        // Rank based on the walk
        let total_capitals = capitals.iter().filter(|&&c| c).count();
        let denom = (n * (n + 1) + 1) as f64;
        let mut score = if n == capitals_touched {
            (denom - 1.0) / denom
        } else {
            // unsigned wrap-around, as in the original, when a run touched more capitals than the filter is long
            let subtract = substrings.wrapping_mul(n).wrapping_add(n.wrapping_sub(capitals_touched)) as f64;
            (denom - subtract) / denom
        };
        score += (m - prefix_size.min(m)) as f64 / m as f64 / (2.0 * denom);
        if total_capitals != 0 {
            score += capitals_touched as f64 / total_capitals as f64 / (4.0 * denom);
        }
        score += n as f64 / m as f64 / (8.0 * denom);
        score
    })
}

/// Returns 0 if `filter` does not match `candidate`, otherwise a positive
/// score (higher is better, 1 for an exact match or an empty filter). The
/// ranges of `candidate` covered by the filter are appended to `out`.
pub fn rank(filter: &[u8], candidate: &[u8], mut out: Option<&mut Vec<(usize, usize)>>) -> f64 {
    if filter.is_empty() {
        return 1.0;
    }
    if !is_subset(filter, candidate) {
        return 0.0;
    }
    if filter == candidate {
        if let Some(out) = out.as_deref_mut() {
            out.push((0, filter.len()));
        }
        return 1.0;
    }
    if filter.len().saturating_mul(candidate.len()) > MAX_CELLS {
        return filter.len() as f64 / candidate.len() as f64;
    }
    calculate_rank(filter, candidate, out)
}

#[cfg(test)]
mod tests {
    use super::rank;

    fn r(filter: &str, candidate: &str) -> f64 {
        rank(filter.as_bytes(), candidate.as_bytes(), None)
    }

    #[test]
    fn ordering() {
        assert!(r("otv", "OTVStatusBar.mm") < r("otv", "OakTextView.mm"));
        assert!(r("doc", "OakDocument.mm") < r("doc", "document.cc"));
        assert!(r("rmate", "RMateServer.cc") < r("rmate", "rmate"));
        assert!(r("oakfilechooser", "OakFinderLabelChooser") < r("oakfilechooser", "OakFileChooser"));
        assert_eq!(r("otbv", "OakTextView.mm"), 0.0);
        assert_eq!(r("", "anything"), 1.0);
        assert_eq!(r("x", ""), 0.0);
    }

    #[test]
    fn ranges() {
        let mut out = Vec::new();
        assert!(rank(b"otv", b"OakTextView.mm", Some(&mut out)) > 0.0);
        assert_eq!(out, vec![(0, 1), (3, 4), (7, 8)]);

        out.clear();
        assert_eq!(rank(b"rmate", b"rmate", Some(&mut out)), 1.0);
        assert_eq!(out, vec![(0, 5)]);
    }

    #[test]
    fn no_nan_without_word_starts() {
        // “g” follows a period, so nothing in the name counts as a word start
        let score = r("git", ".gitignore");
        assert!(score.is_finite() && score > 0.0, "{score}");
    }

    #[test]
    fn long_candidates_still_match() {
        let long = format!("{}/needle.txt", "directory/".repeat(200));
        let score = r("needle", &long);
        assert!(score > 0.0 && score < 0.01, "{score}");
    }

    #[test]
    fn never_panics_and_never_nan() {
        // every pair of short strings over a small alphabet that includes the awkward bytes
        let alphabet: &[u8] = b"aA.' _\xC3\xA9-1";
        let mut strings: Vec<Vec<u8>> = vec![vec![]];
        for len in 1..=4 {
            let mut idx = vec![0usize; len];
            loop {
                strings.push(idx.iter().map(|&i| alphabet[i]).collect());
                let mut k = 0;
                while k < len {
                    idx[k] += 1;
                    if idx[k] < alphabet.len() {
                        break;
                    }
                    idx[k] = 0;
                    k += 1;
                }
                if k == len {
                    break;
                }
            }
        }
        for filter in strings.iter().filter(|s| s.len() <= 3) {
            for candidate in &strings {
                let mut out = Vec::new();
                let score = rank(filter, candidate, Some(&mut out));
                assert!(!score.is_nan(), "{filter:?} {candidate:?}");
                for &(from, to) in &out {
                    assert!(from <= to && to <= candidate.len(), "{filter:?} {candidate:?} {out:?}");
                }
            }
        }
    }
}
