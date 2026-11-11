#include <cmath>
#include <cstdint>
#include <cstdio>

#include "font.h"
#include "framework/canvas.h"
#include "text.h"

// (x, y) のピクセルを color にする
// Set the pixel at (x, y) to color.
void set_pixel(Canvas& canvas, int x, int y, Color color) {
    // 画面の外は何もしない。はみ出して pixels の外を書くと、プログラムが壊れるため
    // Do nothing outside the screen. Writing past pixels would break the program.
    if (x < 0 || x >= canvas.width || y < 0 || y >= canvas.height) {
        return;
    }
    canvas.pixels[y * canvas.width + x] = color;
}

// グリフの輪郭の点を、画面に点として打つ
// (origin_x, baseline_y) は、フォントの座標 (0, 0) を置く画面の位置。scale は、フォントの単位 1 あたりのピクセル数
// Plot glyph outline points on screen.
// (origin_x, baseline_y) is the screen position for font coordinate (0, 0). scale is the number of pixels per font unit.
void draw_glyph_points(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale, Color color) {
    for (int i = 0; i < static_cast<int>(glyph.points.size()); i++) {
        const Point& point = glyph.points[i];

        const float x = origin_x + static_cast<float>(point.x) * scale;
        // フォントの y は上向き、画面の y は下向き。足すと上下が逆になるので、引く
        // Font y points up, but screen y points down. Subtract to convert between them.
        const float y = baseline_y - static_cast<float>(point.y) * scale;

        // 1ピクセルでは小さくて見えにくいので、2×2 ピクセルの四角にする。四角の真ん中が、点の位置に来るようにずらす
        // One pixel is too small to see clearly, so use a 2x2 square. Offset it so the point is at the square center.
        const int left = static_cast<int>(std::round(x)) - 1;
        const int top = static_cast<int>(std::round(y)) - 1;
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                set_pixel(canvas, left + dx, top + dy, color);
            }
        }
    }
}

int main() {
    Canvas canvas(320, 180, 4);  // 320×180 の絵を、4倍に拡大して表示する / Display a 320x180 image enlarged 4 times.

    // 読むフォント。Windows に入っているものを、実行のときに読む（フォントのファイルは配布しない）
    // 游ゴシック（Yu Gothic）。ラテン文字も日本語も、TrueType の輪郭で入っている
    // .ttc は、1つのファイルに複数のフォントが入っているもの。先頭の1つを使う
    // 別のフォントを試すときは、ここを書き換える
    // Font to read. Load an installed Windows font at runtime (font files are not included).
    // Yu Gothic. Both Latin and Japanese glyphs use TrueType outlines.
    // A .ttc file contains multiple fonts in one file. Use the first one.
    // Change this path here to try another font.
    const char* font_path = "C:/Windows/Fonts/YuGothR.ttc";

    // ループに入る前に、1回だけ読む。毎フレーム読むと、十数MBのファイルを何度も読み直すことになる
    // Read once before the loop. Reading it every frame would repeatedly load a file over 10 MB.
    Font font;
    Glyph glyph_a;
    Glyph glyph_hiragana;
    char info[256];
    const bool is_loaded = load_font(font, font_path);
    if (is_loaded) {
        // 文字コードから、グリフの番号を求めて、輪郭の点を読む
        // Find the glyph ID from the character code and read its outline points.
        const int index_a = find_glyph_index(font, 'A');
        const int index_hiragana = find_glyph_index(font, U'あ');
        glyph_a = load_glyph(font, index_a);
        glyph_hiragana = load_glyph(font, index_hiragana);

        std::printf("'A': glyph %d, %d points, %d contours\n", index_a, static_cast<int>(glyph_a.points.size()), static_cast<int>(glyph_a.contour_ends.size()));
        std::printf("U+3042: glyph %d, %d points, %d contours\n", index_hiragana, static_cast<int>(glyph_hiragana.points.size()), static_cast<int>(glyph_hiragana.contour_ends.size()));

        // 読んだ値を、画面にも出す。日本語の文字は draw_text では出せないので、文字コードで書く
        // Also show the read values on screen. draw_text cannot display Japanese, so write the code point.
        std::snprintf(info, sizeof(info), "tables %d  units_per_em %d\nglyphs %d  loca format %d\nA      glyph %d, %d points\nU+3042 glyph %d, %d points",
            font.num_tables, font.units_per_em,
            font.num_glyphs, font.index_to_loc_format,
            index_a, static_cast<int>(glyph_a.points.size()),
            index_hiragana, static_cast<int>(glyph_hiragana.points.size()));
    } else {
        std::snprintf(info, sizeof(info), "cannot read the font file\n%s", font_path);
    }

    while (canvas.update()) {
        // 毎フレーム、背景を暗い青で塗り直す。点が見やすいように、無地にする
        // Repaint the background solid dark blue every frame so the points are easy to see.
        const Color background = {16, 16, 48};
        for (int y = 0; y < canvas.height; y++) {
            for (int x = 0; x < canvas.width; x++) {
                set_pixel(canvas, x, y, background);
            }
        }

        const Color white = {255, 255, 255};
        draw_text(canvas, 4, 4, info, white, 1);

        if (is_loaded) {
            // 文字の大きさ（1 em）を、150 ピクセルにする。フォントの単位 1 が何ピクセルになるかは、1 em の単位数で割って求める
            // Set text size (1 em) to 150 pixels. Divide by the font units per em to find pixels per font unit.
            const float em_pixels = 150.0f;
            const float scale = em_pixels / static_cast<float>(font.units_per_em);

            // 文字を並べる基準の線（ベースライン）の高さ。文字はこの線の上に乗る
            // Height of the baseline used to position glyphs. Glyphs sit on this line.
            const float baseline_y = 165.0f;

            const Color yellow = {255, 224, 64};
            draw_glyph_points(canvas, glyph_a, 30.0f, baseline_y, scale, yellow);
            draw_glyph_points(canvas, glyph_hiragana, 150.0f, baseline_y, scale, yellow);
        }

        canvas.present();
    }
}
