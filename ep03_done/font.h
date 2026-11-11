#pragma once

#include <cstdint>
#include <vector>

// TrueType フォントのファイルを読む関数と、読んだ結果を入れる構造体
// 位置や大きさの数は「フォントの単位」で、画面のピクセルではない。x は右、y は上が正の向き
// Functions for reading TrueType font files and structures that hold the results.
// Positions and sizes use font units, not screen pixels. Positive x points right and positive y points up.

// 輪郭の点を1つ
// One outline point.
struct Point {
    int x;
    int y;
    // 曲線の上の点なら true、曲線の外にあって形を引っ張る点なら false
    // True for an on-curve point; false for an off-curve point that pulls the shape.
    bool on_curve;  
};

// 文字1つ分の輪郭
// Outline for one glyph.
struct Glyph {
    std::vector<Point> points;  // すべての輪郭の点が、順に並ぶ / Points from all contours, in order.
    // 輪郭ごとの、最後の点の番号（points の添字）。「あ」のように離れた部分や穴があると、輪郭は複数になる
    // Index in points of the last point in each contour. A glyph such as あ can have multiple separate contours or holes.
    std::vector<int> contour_ends;  
};

// 読み込んだフォント
// Loaded font.
struct Font {
    std::vector<uint8_t> data;  // ファイルの中身を、丸ごと入れる / Store the entire file contents.
    size_t directory_offset = 0;  // 表の目次の位置（ファイルの先頭から数える） / Offset of the table directory from the start of the file.
    int num_tables = 0;  // 表の数 / Number of tables.
    // 文字の大きさ（1 em）を、フォントの単位で何個分にするか（head の値）
    // Number of font units in the text size (1 em), from head.
    int units_per_em = 0;  
    // loca の形。0 は短い形、1 は長い形（head の値）
    // loca format: 0 is short and 1 is long (value from head).
    int index_to_loc_format = 0;  
    int num_glyphs = 0;  // グリフの数（maxp の値） / Number of glyphs (value from maxp).
    // 文字コードからグリフの番号を引く表の位置（cmap の中の、形式 4 の表）
    // Offset of the table mapping character codes to glyph IDs (format 4 in cmap).
    size_t cmap_offset = 0;  
    // グリフの番号から、glyf の中の位置を引く表の位置
    // Offset of the table that maps glyph IDs to positions in glyf.
    size_t loca_offset = 0;  
    size_t glyf_offset = 0;  // グリフの輪郭が並ぶ表の位置 / Offset of the table containing glyph outlines.
};

// ファイルを丸ごと読んで、バイトの列にする。読めたら true
// Read the whole file into a byte array. Return true if reading succeeds.
bool read_file(const char* path, std::vector<uint8_t>& bytes);

// バイトの列の offset 番目から、整数を読む
// Read an integer from a byte array at offset.
uint16_t read_u16(const std::vector<uint8_t>& bytes, size_t offset);  // 2バイトの整数（0 以上） / A 2-byte unsigned integer.
uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset);  // 4バイトの整数（0 以上） / A 4-byte unsigned integer.
int16_t read_i16(const std::vector<uint8_t>& bytes, size_t offset);  // 2バイトの整数（マイナスにもなる） / A 2-byte signed integer.

// フォントのファイルを読んで、font に入れる。読めて、この回で扱える形なら true
// Read a font file into font. Return true if it loads and uses a format supported by this episode.
bool load_font(Font& font, const char* path);

// 文字コード（Unicode）から、グリフの番号を求める。フォントにない文字は 0（「豆腐」の四角が入っている番号）
// Find the glyph ID from a Unicode code point. Return 0 for characters missing from the font (the missing-character box).
int find_glyph_index(const Font& font, uint32_t char_code);

// グリフの番号から、輪郭の点を読む。複合グリフ（ほかのグリフを組み合わせたもの）は読まず、点が空のグリフを返す
// Read outline points by glyph ID. Do not read composite glyphs (made from other glyphs); return an empty glyph instead.
Glyph load_glyph(const Font& font, int glyph_index);
