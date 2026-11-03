#include "render/Overlay.hpp"

#include <array>
#include <cstdint>

namespace camview::overlay {

namespace {

// One glyph: 7 rows, each the lowest 5 bits, most significant bit = leftmost pixel.
using Glyph = std::array<std::uint8_t, kGlyphHeight>;

const Glyph* glyphFor(char c) {
    static constexpr Glyph k0{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110};
    static constexpr Glyph k1{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    static constexpr Glyph k2{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111};
    static constexpr Glyph k3{0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110};
    static constexpr Glyph k4{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010};
    static constexpr Glyph k5{0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110};
    static constexpr Glyph k6{0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110};
    static constexpr Glyph k7{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000};
    static constexpr Glyph k8{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110};
    static constexpr Glyph k9{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100};
    static constexpr Glyph kA{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001};
    static constexpr Glyph kC{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110};
    static constexpr Glyph kD{0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110};
    static constexpr Glyph kE{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111};
    static constexpr Glyph kF{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000};
    static constexpr Glyph kG{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111};
    static constexpr Glyph kI{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110};
    static constexpr Glyph kL{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111};
    static constexpr Glyph kM{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001};
    static constexpr Glyph kN{0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001};
    static constexpr Glyph kO{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110};
    static constexpr Glyph kP{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000};
    static constexpr Glyph kR{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001};
    static constexpr Glyph kS{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110};
    static constexpr Glyph kT{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100};
    static constexpr Glyph kX{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001};
    static constexpr Glyph kDot{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100};
    static constexpr Glyph kColon{0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000};
    static constexpr Glyph kDash{0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000};

    switch (c) {
    case '0':
        return &k0;
    case '1':
        return &k1;
    case '2':
        return &k2;
    case '3':
        return &k3;
    case '4':
        return &k4;
    case '5':
        return &k5;
    case '6':
        return &k6;
    case '7':
        return &k7;
    case '8':
        return &k8;
    case '9':
        return &k9;
    case 'A':
        return &kA;
    case 'C':
        return &kC;
    case 'D':
        return &kD;
    case 'E':
        return &kE;
    case 'F':
        return &kF;
    case 'G':
        return &kG;
    case 'I':
        return &kI;
    case 'L':
        return &kL;
    case 'M':
        return &kM;
    case 'N':
        return &kN;
    case 'O':
        return &kO;
    case 'P':
        return &kP;
    case 'R':
        return &kR;
    case 'S':
        return &kS;
    case 'T':
        return &kT;
    case 'X':
        return &kX;
    case '.':
        return &kDot;
    case ':':
        return &kColon;
    case '-':
        return &kDash;
    default:
        return nullptr;
    }
}

bool isLit(const Glyph& glyph, int row, int column) {
    const int bits = glyph[static_cast<std::size_t>(row)];
    return ((bits >> (kGlyphWidth - 1 - column)) & 1) != 0;
}

} // namespace

void layoutText(std::string_view text, int x, int y, int scale, std::vector<OverlayRect>& rects) {
    int cursorX = x;
    for (char c : text) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (const Glyph* glyph = glyphFor(c)) {
            for (int row = 0; row < kGlyphHeight; ++row) {
                int column = 0;
                while (column < kGlyphWidth) {
                    if (!isLit(*glyph, row, column)) {
                        ++column;
                        continue;
                    }
                    const int runStart = column;
                    while (column < kGlyphWidth && isLit(*glyph, row, column)) {
                        ++column;
                    }
                    rects.push_back({cursorX + runStart * scale, y + row * scale,
                                     (column - runStart) * scale, scale});
                }
            }
        }
        cursorX += kGlyphAdvance * scale;
    }
}

int textWidth(std::string_view text, int scale) {
    return static_cast<int>(text.size()) * kGlyphAdvance * scale;
}

} // namespace camview::overlay