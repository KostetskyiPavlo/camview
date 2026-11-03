#pragma once

#include <string_view>
#include <vector>

namespace camview {

struct OverlayRect {
    int x;
    int y;
    int width;
    int height;
};

// Tiny built-in 5x7 bitmap font, so the overlay needs neither font files nor SDL_ttf.
// Supports digits, space, '.', ':', '-' and the upper-case letters camview's overlay uses;
// lower-case letters are drawn as upper-case, any other character as a blank cell.
// Pure layout logic without SDL, so it can be unit-tested.
namespace overlay {

constexpr int kGlyphWidth = 5;
constexpr int kGlyphHeight = 7;
constexpr int kGlyphAdvance = 6; // Glyph plus one column of spacing.
constexpr int kLineAdvance = 9;  // Glyph plus two rows of spacing.

// Appends rectangles covering the lit pixels of `text` with its top-left corner at (x, y).
// Each font pixel is scale x scale screen pixels; horizontal runs are merged into one rect.
void layoutText(std::string_view text, int x, int y, int scale, std::vector<OverlayRect>& rects);

[[nodiscard]] int textWidth(std::string_view text, int scale);

} // namespace overlay

} // namespace camview