#include <algorithm>
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

// (x0, y0) から (x1, y1) まで、1ピクセルずつ進みながら線を引く
// Draw a line from (x0, y0) to (x1, y1), moving one pixel at a time.
void draw_line(Canvas& canvas, int x0, int y0, int x1, int y1, Color color) {
    const int dx = x1 - x0;
    const int dy = y1 - y0;

    // x と y のうち長いほうの軸で数える。短いほうは1歩で1マス以下しか動かないので、間が空かない
    // Count along the longer of the x and y axes. The shorter axis moves by at most one cell per step, so there are no gaps.
    const int steps = std::max(std::abs(dx), std::abs(dy));

    // 始点と終点が同じ場所だと steps が 0 になり、下の割り算ができない。点を1つ打って終わる
    // If the start and end are the same, steps is 0 and the division below is not possible. Draw one point and return.
    if (steps == 0) {
        set_pixel(canvas, x0, y0, color);
        return;
    }

    for (int i = 0; i <= steps; i++) {
        // t は 0（始点）から 1（終点）まで進む割合
        // t is the fraction along the line, from 0 (start) to 1 (end).
        const float t = static_cast<float>(i) / static_cast<float>(steps);

        // 切り捨て（static_cast<int> だけ）だと、いつも 0 の向きに削れて線が片側にずれる。最も近いピクセルに丸める
        // Truncating with static_cast<int> always rounds toward 0 and shifts the line to one side. Round to the nearest pixel.
        const int x = x0 + static_cast<int>(std::round(static_cast<float>(dx) * t));
        const int y = y0 + static_cast<int>(std::round(static_cast<float>(dy) * t));
        set_pixel(canvas, x, y, color);
    }
}

// グリフの輪郭の点を、画面に点として打つ
// (origin_x, baseline_y) は、フォントの座標 (0, 0) を置く画面の位置。scale は、フォントの単位 1 あたりのピクセル数
// この回で、この関数を、点の種類（曲線の上か、外か、補った点か）で色を変える形に直す。引数の color はなくなる
// Plot glyph outline points on screen.
// (origin_x, baseline_y) is the screen position for font coordinate (0, 0). scale is the number of pixels per font unit.
// In this episode, color points by type (on-curve, off-curve, or added). Remove the color argument.
void draw_glyph_points(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale, Color color) {
    for (int i = 0; i < static_cast<int>(glyph.points.size()); i++) {
        const Point& point = glyph.points[i];

        // この回で、フォントの点を画面の位置に変えるここ（x と y）を、to_screen として取り出す
        // In this episode, extract the conversion from font point (x, y) to screen position into to_screen.
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

// ここに、この回で書くものを足していく
//   Vec2                  … 画面上の位置。ピクセルの間の位置も表せるように、小数で持つ（構造体。draw_glyph_points より上に書く）
//   OutlinePoint          … 輪郭の点を1つ。画面の位置と、曲線の上の点かどうかと、補った点かどうか（構造体）
//   curve_segment_length  … 曲線を分ける線分の長さの目安（ピクセル）
//   to_screen             … フォントの点を、画面の位置に変える関数（draw_glyph_points の中から取り出す。draw_glyph_points より上に書く）
//   draw_line（Vec2 版）  … 小数の位置を受け取って、線を引く関数。上の draw_line を呼ぶ
//   draw_quadratic_bezier … 2次ベジェ曲線を、細かい線分に分けて引く関数
//   make_outline          … グリフの輪郭を、画面の位置にして、輪郭ごとの点の列に並べ直す関数
//   draw_contour          … 点の列（輪郭1つ分）を、線と曲線で描く関数
//   draw_glyph_outline    … グリフの輪郭すべてを、線と曲線で描く関数
// Add the items for this episode here.
// Vec2                  ... Screen position stored as decimals so it can represent positions between pixels (struct; add above draw_glyph_points).
// OutlinePoint          ... One outline point: screen position, whether it is on a curve, and whether it was added (struct).
// curve_segment_length  ... Target length in pixels for curve segments.
// to_screen             ... Convert a font point to a screen position (extract from draw_glyph_points; add above it).
// draw_line (Vec2 version) ... Draw a line from decimal positions. Calls the draw_line above.
// draw_quadratic_bezier ... Draw a quadratic Bezier curve as short line segments.
// make_outline          ... Convert a glyph outline to screen positions and group points by contour.
// draw_contour          ... Draw one contour (a list of points) with lines and curves.
// draw_glyph_outline    ... Draw all glyph contours with lines and curves.

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

    // この回で、ここに show_points（輪郭の点を重ねて出すか）の切り替えを足す。変数を書き換えて切り替える
    // この回で、ここの A を S に替える（S は曲線が多い）。glyph_a、index_a、画面に出す info も S に直す
    // Add a show_points toggle here to control whether outline points are overlaid. Toggle it by changing the variable.
    // Replace A with S here (S has more curves). Also change glyph_a, index_a, and the displayed info to S.

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

            // この回で、ここ（点を打つところ）の前に、輪郭を線と曲線で描く処理を足す
            // 点は、show_points が true のときだけ、輪郭の上に重ねて出す
            // Add code before plotting points to draw outlines with lines and curves.
            // Overlay points on the outline only when show_points is true.
            const Color yellow = {255, 224, 64};
            draw_glyph_points(canvas, glyph_a, 30.0f, baseline_y, scale, yellow);
            draw_glyph_points(canvas, glyph_hiragana, 150.0f, baseline_y, scale, yellow);
        }

        canvas.present();
    }
}
