#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "font.h"
#include "framework/canvas.h"
#include "text.h"

// 画面上の位置。ピクセルの間の位置も表せるように、小数で持つ
// Screen position stored as decimals so it can represent positions between pixels.
struct Vec2 {
    float x;
    float y;
};

// 輪郭の点を1つ。画面の位置と、曲線の上の点かどうか
// One outline point: its screen position and whether it lies on the curve.
struct OutlinePoint {
    Vec2 position;
    bool on_curve;
    // フォントには書かれていなくて、あとから補った上の点なら true
    // True if this is an added on-curve point that is not stored in the font.
    bool implied;  
};

// 曲線を分ける線分の長さの目安（ピクセル）。短いほどなめらかになるが、線の数が増える
// Target length in pixels for curve segments. Shorter segments are smoother but increase the number of lines.
const float curve_segment_length = 2.0f;

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

// 小数の位置で受け取って、線を引く
// ピクセル (x, y) は、x 〜 x+1、y 〜 y+1 の範囲を受け持つ。だから、小数の位置が入っているピクセルの番号は、切り捨て（floor）で決まる
// Draw a line from decimal positions.
// Pixel (x, y) covers x to x+1 and y to y+1, so the containing pixel index is found by rounding down (floor).
void draw_line(Canvas& canvas, Vec2 a, Vec2 b, Color color) {
    const int x0 = static_cast<int>(std::floor(a.x));
    const int y0 = static_cast<int>(std::floor(a.y));
    const int x1 = static_cast<int>(std::floor(b.x));
    const int y1 = static_cast<int>(std::floor(b.y));
    draw_line(canvas, x0, y0, x1, y1, color);
}

// フォントの点を、画面の位置に変える
// (origin_x, baseline_y) は、フォントの座標 (0, 0) を置く画面の位置。scale は、フォントの単位 1 あたりのピクセル数
// Convert a font point to screen position.
// (origin_x, baseline_y) is the screen position for font coordinate (0, 0). scale is the number of pixels per font unit.
Vec2 to_screen(const Point& point, float origin_x, float baseline_y, float scale) {
    const float x = origin_x + static_cast<float>(point.x) * scale;
    // フォントの y は上向き、画面の y は下向き。足すと上下が逆になるので、引く
    // Font y points up, but screen y points down. Subtract to convert between them.
    const float y = baseline_y - static_cast<float>(point.y) * scale;
    return Vec2{x, y};
}

// 2次ベジェ曲線を、細かい線分に分けて引く
// p0 から p2 まで進む。p1 は曲線を引っ張る点で、曲線は p1 までは届かない
// Draw the quadratic Bezier curve as short line segments.
// Move from p0 to p2. p1 pulls the curve, but the curve does not reach p1.
void draw_quadratic_bezier(Canvas& canvas, Vec2 p0, Vec2 p1, Vec2 p2, Color color) {
    // 分ける数は、曲線の長さに合わせる。長い曲線を少ない線分で引くと角ばるし、短い曲線を細かく分けても無駄になるため
    // p0 → p1 → p2 と折れ線でたどった長さは、曲線の長さより少し長い。目安にはそれで足りる
    // Choose the number of segments based on curve length. Too few segments make a long curve look angular; splitting a short curve into many segments wastes work.
    // The length of the polyline p0 to p1 to p2 is slightly longer than the curve, which is a good enough estimate.
    const float length = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y);

    // 3点が同じ場所だと length が 0 になる。分ける数が 0 だと、下の割り算ができないので、最低でも 1 にする
    // If all three points are in the same place, length is 0. A subdivision count of 0 would cause division by zero below, so use at least 1.
    const int steps = std::max(1, static_cast<int>(std::ceil(length / curve_segment_length)));

    Vec2 previous = p0;
    for (int i = 1; i <= steps; i++) {
        // t は 0（p0）から 1（p2）まで進む割合
        // t is the fraction along the curve, from 0 (p0) to 1 (p2).
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float u = 1.0f - t;

        // 2次ベジェ曲線の式。p0 → p1 の線上の点と、p1 → p2 の線上の点を、どちらも割合 t の位置で取り、その2点をさらに割合 t で結んだ点になる
        // 整理すると、3つの点を u*u、2*u*t、t*t の割合で混ぜたもの。この3つの割合は、足すと 1 になる
        // Quadratic Bezier formula. Take points at fraction t on p0 to p1 and p1 to p2, then take the point at fraction t between them.
        // Rearranged, it blends the three points by u*u, 2*u*t, and t*t. These weights add up to 1.
        const Vec2 current = Vec2{
            u * u * p0.x + 2.0f * u * t * p1.x + t * t * p2.x,
            u * u * p0.y + 2.0f * u * t * p1.y + t * t * p2.y,
        };
        draw_line(canvas, previous, current, color);
        previous = current;
    }
}

// グリフの輪郭を、画面の位置にして、輪郭ごとの点の列に並べ直す
// 外の点が2つ続いたら、間に「見えない上の点」を補う。TrueType の決まりで、2つの外の点の真ん中には、曲線の上の点があるものとして扱う
// こうすると、外の点はどれも「前後を上の点にはさまれた1つ」になる。輪郭は輪なので、最後の点と最初の点の間も調べる
// Convert glyph outlines to screen positions and group points by contour.
// When two off-curve points are next to each other, add an invisible on-curve point between them. TrueType treats the midpoint between two off-curve points as an on-curve point.
// This makes every off-curve point sit between two on-curve points. A contour is closed, so also check between its last and first points.
std::vector<std::vector<OutlinePoint>> make_outline(const Glyph& glyph, float origin_x, float baseline_y, float scale) {
    std::vector<std::vector<OutlinePoint>> contours;

    for (int c = 0; c < static_cast<int>(glyph.contour_ends.size()); c++) {
        // contour_ends は輪郭の最後の点の番号。最初の点は、前の輪郭の最後の点の次
        // contour_ends stores the index of each contour's last point. The first point follows the previous contour's last point.
        const int first = (c == 0) ? 0 : glyph.contour_ends[c - 1] + 1;
        const int count = glyph.contour_ends[c] - first + 1;

        // 点が2つ未満の輪郭は、線にならないので飛ばす
        // Skip contours with fewer than two points; they cannot form a line.
        if (count < 2) {
            continue;
        }

        std::vector<OutlinePoint> points;
        for (int i = 0; i < count; i++) {
            const Point& current = glyph.points[first + i];
            const Point& next = glyph.points[first + (i + 1) % count];

            const Vec2 current_position = to_screen(current, origin_x, baseline_y, scale);
            points.push_back(OutlinePoint{current_position, current.on_curve, false});

            if (!current.on_curve && !next.on_curve) {
                const Vec2 next_position = to_screen(next, origin_x, baseline_y, scale);
                const Vec2 middle = Vec2{(current_position.x + next_position.x) * 0.5f, (current_position.y + next_position.y) * 0.5f};
                points.push_back(OutlinePoint{middle, true, true});
            }
        }
        contours.push_back(points);
    }

    return contours;
}

// 輪郭を1つ描く。points は make_outline で並べ直した、1つの輪郭の点
// Draw one contour. points contains the reordered points for one contour from make_outline.
void draw_contour(Canvas& canvas, const std::vector<OutlinePoint>& points, Color color) {
    // 輪はどこから始めてもよいので、最初に見つかった曲線の上の点から始める
    // 外の点の間には必ず上の点を補ったので、上の点は必ず見つかる
    // A contour can start anywhere, so start at the first on-curve point found.
    // An on-curve point was added between any two off-curve points, so one is always present.
    int start = 0;
    while (!points[start].on_curve) {
        start++;
    }

    // start の次の点から、start に戻ってくるまで、1周する
    // Make one loop from the point after start back to start.
    const int total = static_cast<int>(points.size());
    Vec2 current = points[start].position;
    int i = 1;
    while (i <= total) {
        const OutlinePoint& next = points[(start + i) % total];
        if (next.on_curve) {
            // 次も上の点なら、まっすぐな線
            // If the next point is also on-curve, draw a straight line.
            draw_line(canvas, current, next.position, color);
            current = next.position;
            i++;
        } else {
            // 次が外の点なら、その次は必ず上の点。外の点に引っ張られる曲線で、そこまで進む
            // If the next point is off-curve, the point after it is on-curve. Follow the curve pulled by the off-curve point to that point.
            const OutlinePoint& end = points[(start + i + 1) % total];
            draw_quadratic_bezier(canvas, current, next.position, end.position, color);
            current = end.position;
            i += 2;
        }
    }
}

// グリフの輪郭を、線と曲線で描く
// 「あ」のように、輪郭が複数あることもある。輪郭ごとに、最後の点から最初の点に戻って閉じる
// Draw glyph outlines with lines and curves.
// A glyph such as あ can have multiple contours. Close each contour by joining its last point back to its first.
void draw_glyph_outline(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale, Color color) {
    const std::vector<std::vector<OutlinePoint>> contours = make_outline(glyph, origin_x, baseline_y, scale);
    for (int c = 0; c < static_cast<int>(contours.size()); c++) {
        draw_contour(canvas, contours[c], color);
    }
}

// グリフの輪郭の点を、画面に点として打つ
// 曲線の上の点は緑、外の点は赤、補った上の点は水色にする。輪郭が正しく描けているか、点の種類から確かめるため
// Plot glyph outline points on screen.
// Use green for on-curve points, red for off-curve points, and cyan for added on-curve points. Point types help check whether the outline is drawn correctly.
void draw_glyph_points(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale) {
    const Color green = {64, 255, 64};
    const Color red = {255, 64, 64};
    const Color light_blue = {64, 200, 255};

    // 描くときと同じ、補った点も入った並びを使う
    // Use the same point sequence as drawing, including added points.
    const std::vector<std::vector<OutlinePoint>> contours = make_outline(glyph, origin_x, baseline_y, scale);
    for (int c = 0; c < static_cast<int>(contours.size()); c++) {
        for (int i = 0; i < static_cast<int>(contours[c].size()); i++) {
            const OutlinePoint& point = contours[c][i];

            Color color = red;
            if (point.implied) {
                color = light_blue;
            } else if (point.on_curve) {
                color = green;
            }

            // 1ピクセルでは小さくて見えにくいので、2×2 ピクセルの四角にする。四角の真ん中が、点の位置に来るようにずらす
            // One pixel is too small to see clearly, so use a 2x2 square. Offset it so the point is at the square center.
            const int left = static_cast<int>(std::round(point.position.x)) - 1;
            const int top = static_cast<int>(std::round(point.position.y)) - 1;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    set_pixel(canvas, left + dx, top + dy, color);
                }
            }
        }
    }
}

int main() {
    Canvas canvas(320, 180, 4);  // 320×180 の絵を、4倍に拡大して表示する / Display a 320x180 image enlarged 4 times.

    // 読むフォント。Windows に入っているものを、実行のときに読む（フォントのファイルは配布しない）
    // 游ゴシック（Yu Gothic）。ラテン文字も日本語も、TrueType の輪郭で入っている
    // .ttc は、1つのファイルに複数のフォントが入っているもの。先頭の1つを使う
    // 別のフォントを試すときは、ここを書き換える（例：メイリオは "C:/Windows/Fonts/meiryo.ttc"）
    // 游ゴシックは、外の点が2つ続くところに上の点を書いてくれているので、補う場所がほとんどない。メイリオなどは、補う場所がたくさんある
    // Font to read. Load an installed Windows font at runtime (font files are not included).
    // Yu Gothic. Both Latin and Japanese glyphs use TrueType outlines.
    // A .ttc file contains multiple fonts in one file. Use the first one.
    // Change this path to try another font (for example, Meiryo is "C:/Windows/Fonts/meiryo.ttc").
    // Yu Gothic stores on-curve points between consecutive off-curve points, so few need to be added. Fonts such as Meiryo need many more added points.
    const char* font_path = "C:/Windows/Fonts/YuGothR.ttc";

    // true にすると、輪郭の点を重ねて出す。緑は曲線の上の点、赤は曲線の外の点、水色は補った上の点
    // If true, overlay the outline points. Green points are on the curve, red points are off the curve, and cyan points are added points on the curve.
    const bool show_points = false;

    // ループに入る前に、1回だけ読む。毎フレーム読むと、十数MBのファイルを何度も読み直すことになる
    // Read once before the loop. Reading it every frame would repeatedly load a file over 10 MB.
    Font font;
    Glyph glyph_s;
    Glyph glyph_hiragana;
    char info[256];
    const bool is_loaded = load_font(font, font_path);
    if (is_loaded) {
        // 文字コードから、グリフの番号を求めて、輪郭の点を読む
        // Find the glyph ID from the character code and read its outline points.
        const int index_s = find_glyph_index(font, 'S');
        const int index_hiragana = find_glyph_index(font, U'あ');
        glyph_s = load_glyph(font, index_s);
        glyph_hiragana = load_glyph(font, index_hiragana);

        std::printf("'S': glyph %d, %d points, %d contours\n", index_s, static_cast<int>(glyph_s.points.size()), static_cast<int>(glyph_s.contour_ends.size()));
        std::printf("U+3042: glyph %d, %d points, %d contours\n", index_hiragana, static_cast<int>(glyph_hiragana.points.size()), static_cast<int>(glyph_hiragana.contour_ends.size()));

        // 読んだ値を、画面にも出す。日本語の文字は draw_text では出せないので、文字コードで書く
        // Also show the read values on screen. draw_text cannot display Japanese, so write the code point.
        std::snprintf(info, sizeof(info), "S      glyph %d, %d points\nU+3042 glyph %d, %d points",
            index_s, static_cast<int>(glyph_s.points.size()),
            index_hiragana, static_cast<int>(glyph_hiragana.points.size()));
    } else {
        std::snprintf(info, sizeof(info), "cannot read the font file\n%s", font_path);
    }

    while (canvas.update()) {
        // 毎フレーム、背景を暗い青で塗り直す。線が見やすいように、無地にする
        // Repaint the background solid dark blue every frame so the lines are easy to see.
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

            draw_glyph_outline(canvas, glyph_s, 30.0f, baseline_y, scale, white);
            draw_glyph_outline(canvas, glyph_hiragana, 150.0f, baseline_y, scale, white);

            // 点は、輪郭の上に重ねる
            // Overlay points on the outline.
            if (show_points) {
                draw_glyph_points(canvas, glyph_s, 30.0f, baseline_y, scale);
                draw_glyph_points(canvas, glyph_hiragana, 150.0f, baseline_y, scale);
            }
        }

        canvas.present();
    }
}
