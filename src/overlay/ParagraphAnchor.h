#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// Content-anchored paragraph placement ("tự chỉnh lại vị trí khi đặt sai").
//
// The overlay places every source paragraph at the lines it came from. That mapping used to be a
// running line INDEX (paragraph 1 = lines 0..n1, paragraph 2 = n1..n2, ...): the moment the text
// assembler merged, split or dropped a single line, every following paragraph was placed one line
// off - and nothing could ever notice, because the index arithmetic always "adds up".
//
// This module locates each paragraph by MATCHING ITS SOURCE TEXT against the actual source lines
// instead: each paragraph is searched for near the position the previous one ended (a small forward
// window), scored by how many of its leading words appear in a line, and snapped to the best
// candidate. A misplaced paragraph therefore re-anchors itself on the very next tick.
namespace ParagraphAnchor {

// `paragraphTexts`: one entry per source paragraph (its source text, as the translator exposes it).
// `sourceLines`:   the entry's real source lines (the OCR line texts, top to bottom).
// `lineCounts`:    how many source lines each paragraph is supposed to occupy (its own hint).
// Returns one start line index per paragraph, monotonically non-decreasing.
QList<int> anchor(const QStringList &paragraphTexts, const QStringList &sourceLines,
                  const QList<int> &lineCounts);

} // namespace ParagraphAnchor
