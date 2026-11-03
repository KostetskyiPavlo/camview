#include "render/Overlay.hpp"

#include <gtest/gtest.h>
#include <vector>

namespace camview {
namespace {

std::vector<OverlayRect> layout(std::string_view text, int scale = 1) {
    std::vector<OverlayRect> rects;
    overlay::layoutText(text, 0, 0, scale, rects);
    return rects;
}

bool sameRects(const std::vector<OverlayRect>& a, const std::vector<OverlayRect>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].width != b[i].width ||
            a[i].height != b[i].height) {
            return false;
        }
    }
    return true;
}

TEST(Overlay, EverySupportedCharacterDrawsInsideItsCell) {
    for (const char c : std::string_view("0123456789ACDEFGILMNOPRSTX.:-")) {
        const std::vector<OverlayRect> rects = layout(std::string_view(&c, 1));
        EXPECT_FALSE(rects.empty()) << "glyph '" << c << "' is blank";
        for (const OverlayRect& rect : rects) {
            EXPECT_GE(rect.x, 0) << c;
            EXPECT_GE(rect.y, 0) << c;
            EXPECT_LE(rect.x + rect.width, overlay::kGlyphWidth) << c;
            EXPECT_LE(rect.y + rect.height, overlay::kGlyphHeight) << c;
        }
    }
}

TEST(Overlay, UnknownCharactersAreBlankButStillTakeSpace) {
    EXPECT_TRUE(layout("?#").empty());
    EXPECT_EQ(overlay::textWidth("?#", 1), 2 * overlay::kGlyphAdvance);
}

TEST(Overlay, LowerCaseIsDrawnAsUpperCase) {
    EXPECT_TRUE(sameRects(layout("ms"), layout("MS")));
}

TEST(Overlay, MergesAHorizontalRunIntoOneRectangle) {
    // The dash is a single full-width row.
    const std::vector<OverlayRect> rects = layout("-");
    ASSERT_EQ(rects.size(), 1U);
    EXPECT_EQ(rects[0].width, overlay::kGlyphWidth);
}

TEST(Overlay, ScaleMultipliesEveryCoordinate) {
    constexpr int kScale = 3;
    const std::vector<OverlayRect> small = layout("8");
    const std::vector<OverlayRect> large = layout("8", kScale);
    ASSERT_EQ(small.size(), large.size());
    for (std::size_t i = 0; i < small.size(); ++i) {
        EXPECT_EQ(large[i].x, small[i].x * kScale);
        EXPECT_EQ(large[i].y, small[i].y * kScale);
        EXPECT_EQ(large[i].width, small[i].width * kScale);
        EXPECT_EQ(large[i].height, small[i].height * kScale);
    }
}

TEST(Overlay, CharactersAdvanceByOneCellEach) {
    const std::vector<OverlayRect> rects = layout("--");
    ASSERT_EQ(rects.size(), 2U);
    EXPECT_EQ(rects[1].x - rects[0].x, overlay::kGlyphAdvance);
}

} // namespace
} // namespace camview