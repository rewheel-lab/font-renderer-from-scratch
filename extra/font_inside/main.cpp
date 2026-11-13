// 番外編「フォントの中身」の実験用プログラム
// 文#03・#04 で作ったフォントの読み込みと輪郭の描画を使って、フォントのファイルの中に何が入っているかを確かめる
// 入っているのは、輪郭の点と曲線の数字だけ。ほかに、小さい文字のための工夫（ヒンティングの命令、ドット絵、可変フォントの差分）が別の表に入っている
// フォントのファイルは配布しない。実行のときに C:/Windows/Fonts/ から読むので、Windows に入っていないフォントを選ぶと「not found」と出る（落ちずに、ほかのフォントは使える）
// font.h・font.cpp・text.h・text.cpp は ep04_done と同じもの。名前の表（name）や EBLC など、文#03・#04 にない表を読む処理は、この main.cpp に書いてある
// Experiment program for the extra episode "Inside a Font."
// Uses the font loading and outline drawing from Font Renderer #03 and #04 to inspect what is inside a font file.
// The file contains outline points and curve data. Other small-text features (hinting instructions, bitmap glyphs, and variable-font deltas) are stored in separate tables.
// Font files are not included. At runtime, fonts are loaded from C:/Windows/Fonts/. If a font is not installed, the program shows "not found" and continues with other fonts.
// font.h, font.cpp, text.h, and text.cpp are the same as in ep04_done. This main.cpp reads tables not covered in #03 and #04, such as name and EBLC.
//
// 操作
//   1〜6  … フォントを切り替える（1: Yu Gothic、2: Meiryo、3: MS Gothic、4: MS Mincho、5: Noto Sans JP、6: Arial）
//   C     … 次の文字に切り替える（A → S → g → あ → 永 → A …）
//   O     … 輪郭の表示（点の数、曲線の上の点・外の点・補った点の数、グリフのバイト数）
//   T     … 表（テーブル）の一覧。表の名前、バイト数、ファイル全体に対する割合。種類ごとに色分けする
//   M     … TTC の中身。ファイルに入っているフォントの数と、それぞれの名前
//   G     … 1文字のデータの内訳。グリフが何バイトで、そのうちヒンティングの命令が何バイトか
//   P     … 輪郭に点を重ねる、重ねない（緑は曲線の上の点、赤は外の点、水色は補った上の点）
//   Z / X … 拡大、縮小（押している間）。輪郭の表示のとき
//   矢印  … 絵を動かす（押している間）。輪郭の表示のとき
//   R     … 拡大と位置を元に戻す
//   Esc   … 終わる
// フォントや文字を切り替えたときも、画面に出している数字をコンソールに出す
// 画面の下の小さな印は、今の表示（左の4つ）と今のフォント（右の6つ）。明るいのが選ばれているもの
// 表の一覧の色：緑は輪郭（glyf、loca）、橙は小さいサイズ用のドット絵（EBDT、EBLC）、紫は可変フォント（fvar、gvar など）、黄は小さいサイズを整える命令（fpgm、prep、cvt）
// Controls
// 1 to 6 ... Switch fonts (1: Yu Gothic, 2: Meiryo, 3: MS Gothic, 4: MS Mincho, 5: Noto Sans JP, 6: Arial)
// C ... Switch glyph (A, S, g, あ, 永, A, ...).
// O ... Outline info (point count, on-curve, off-curve, and added point counts, and glyph bytes).
// T ... Table list: table names, byte sizes, and share of the file, colored by type.
// M ... TTC contents: font count and names.
// G ... Data breakdown for one glyph: total bytes and bytes used by hinting instructions.
// P ... Toggle outline points (green: on-curve, red: off-curve, cyan: added on-curve).
// Z / X ... Zoom in and out while held, in outline view.
// Arrow keys ... Move the image while held, in outline view.
// R ... Reset zoom and position.
// Esc ... Quit.
// When switching fonts or glyphs, also print the displayed values to the console.
// The small markers at the bottom show the current view (left four) and font (right six). The selected one is bright.
// Table colors: green for outlines (glyf, loca), orange for bitmap glyphs (EBDT, EBLC), purple for variable fonts (fvar, gvar, etc.), yellow for small-size instructions (fpgm, prep, cvt).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
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

// ここから下が、この実験用プログラムで足したもの
// Everything below was added for this experiment program.

// 選べるフォント。キー 1〜6 に、この順で並ぶ
// フォントのファイルは配布しないので、入っていないフォントは「not found」になる
// Fonts that can be selected, in the order assigned to keys 1 through 6.
// Font files are not distributed, so a font that is not installed will be reported as "not found".
struct FontEntry {
    const char* label;  // 画面に出す名前 / The name shown on screen.
    const char* path;  // 読むファイルの場所 / Path of the file to read.
};

const int font_entry_count = 6;
const FontEntry font_entries[font_entry_count] = {
    {"Yu Gothic", "C:/Windows/Fonts/YuGothR.ttc"},
    {"Meiryo", "C:/Windows/Fonts/meiryo.ttc"},
    {"MS Gothic", "C:/Windows/Fonts/msgothic.ttc"},
    {"MS Mincho", "C:/Windows/Fonts/msmincho.ttc"},
    {"Noto Sans JP", "C:/Windows/Fonts/NotoSansJP-VF.ttf"},
    {"Arial", "C:/Windows/Fonts/arial.ttf"},
};

// 切り替えて見る文字。Arial には「あ」と「永」がなく、グリフ 0 の四角（豆腐）が出る
// Glyphs to inspect when switching fonts. Arial does not have あ or 永, so glyph 0 (a missing-character box) appears.
const int sample_count = 5;
const uint32_t sample_chars[sample_count] = {U'A', U'S', U'g', U'あ', U'永'};

// 表示の種類
// View type.
const int view_outline = 0;  // 輪郭 / Outline.
const int view_tables = 1;  // 表の一覧 / Table list.
const int view_members = 2;  // TTC の中身 / TTC contents.
const int view_bytes = 3;  // 1文字のデータの内訳 / Contents of one character.
const int view_count = 4;

// 表の種類。一覧では、種類ごとに色を分ける
// Table type. Use a different color for each type in the list.
const int category_outline = 0;  // glyf、loca
const int category_bitmap = 1;  // EBDT、EBLC、EBSC（小さいサイズ用のドット絵） / EBDT, EBLC, and EBSC (bitmap glyphs for small sizes).
const int category_variable = 2;  // fvar、gvar など（可変フォント） / fvar, gvar, and other variable-font tables.
// fpgm、prep、cvt（小さいサイズを整える命令の元）
// fpgm, prep, and cvt (instructions for adjusting small sizes).
const int category_hint = 3;  
const int category_other = 4;  // それ以外 / Otherwise.
const int category_count = 5;

const char* const category_names[category_count] = {"outline", "bitmap", "variable", "hint", "other"};
const uint32_t category_hexes[category_count] = {0x50DC8C, 0xFFA030, 0xC060FF, 0xFFE040, 0x9090A8};

// グリフの点ごとにあるフラグのビット。font.cpp と同じ意味（あちらは外から見えないので、ここにも書く）
// Flag bits for each glyph point. Same meaning as in font.cpp (they are not visible outside that file, so list them here too).
const int flag_x_short = 0x02;
const int flag_y_short = 0x04;
const int flag_repeat = 0x08;
const int flag_x_same_or_positive = 0x10;
const int flag_y_same_or_positive = 0x20;

// TTC の中身として一覧に出すフォントの数の上限。画面に収まる数
// Maximum number of fonts to list from the TTC. It must fit on screen.
const int max_members = 6;

// 表の目次の1件
// One table-directory entry.
struct TableInfo {
    // "glyf" のような4文字。終わりに '\0' を足して5つ
    // A four-character tag such as "glyf". Add '\0' at the end to make five bytes.
    char tag[5];  
    uint32_t offset;  // ファイルの先頭から数えた位置 / Offset from the start of the file.
    uint32_t length;  // バイト数 / Number of bytes.
};

// TTC に入っているフォント1つ分
// One font in a TTC.
struct MemberInfo {
    char name[48];  // 英語の名前。読めなければ空 / English name. Empty if it cannot be read.
    int table_count;  // 表の数 / Number of tables.
    // そのうち、先に出てきたフォントと同じ場所を指している表の数（ファイルの中に1つしかない表）
    // Number of tables that point to the same locations as in an earlier font (tables stored only once in the file).
    int shared_count;  
    uint32_t total_bytes;  // 表のバイト数の合計（共有している表も数える） / Total table size in bytes, including shared tables.
    uint32_t own_bytes;  // そのうち、共有していない表のバイト数 / Number of bytes in tables that are not shared.
    uint32_t glyf_offset;  // glyf の位置。共有しているか調べるための印 / glyf offset. Used to check whether it is shared.
};

// フォントの読み込み結果
// Font loading result.
const int font_ok = 0;
const int font_not_found = 1;  // ファイルがない / File not found.
const int font_unreadable = 2;  // ファイルはあるが、この回の読み方では読めない / The file exists, but this episode's reader cannot parse it.

// 読んだフォントと、そこから求めておいた数字
// Loaded font and values already calculated from it.
struct FontData {
    int status = font_not_found;
    Font font;
    // このフォント（TTC なら先頭のフォント）の表。大きい順
    // Tables in this font (the first font in a TTC), sorted by size.
    std::vector<TableInfo> tables;  
    uint32_t category_bytes[category_count] = {};  // 種類ごとの表のバイト数の合計 / Total table bytes for each type.
    bool is_ttc = false;
    int member_count = 0;  // ファイルに入っているフォントの数 / Number of fonts in the file.
    std::vector<MemberInfo> members;  // max_members 個まで / Up to max_members.
    int bitmap_size_count = 0;  // ドット絵のサイズの数（EBLC がなければ 0） / Number of bitmap sizes (0 if there is no EBLC).
    int bitmap_min_ppem = 0;  // 一番小さいドット絵の高さ（ピクセル） / Smallest bitmap glyph height in pixels.
    int bitmap_max_ppem = 0;
    int axis_count = 0;  // 可変フォントの軸の数（fvar がなければ 0） / Number of variable-font axes (0 if there is no fvar).
    char axis_tag[5] = {};  // 最初の軸の名前（"wght" は太さ） / Name of the first axis ("wght" means weight).
    float axis_min = 0.0f;
    float axis_default = 0.0f;
    float axis_max = 0.0f;
};

// グリフのデータ（glyf の中の、1つのグリフ）が何バイトで、何にどれだけ使われているか
// Size of one glyph in glyf and how many bytes it uses for each part.
struct GlyphBytes {
    // 複合グリフなら true。内訳は数えない
    // True for a composite glyph. Do not count its components separately.
    bool is_composite = false;  
    // グリフ全体のバイト数（loca の次のグリフの位置との差）
    // Total glyph size in bytes (difference between this and the next glyph offset in loca).
    int total = 0;  
    int header = 0;  // 見出し：輪郭の数と、外側の四角（10バイト） / Header: contour count and bounding box (10 bytes).
    int contour_ends = 0;  // 輪郭ごとの最後の点の番号 / Last-point index for each contour.
    int instruction_size = 0;  // 命令の長さを書く2バイト / 2-byte instruction length.
    int instructions = 0;  // ヒンティングの命令 / Hinting instructions.
    int flags = 0;  // 点ごとのフラグ / Flag for each point.
    int x_coords = 0;  // x の差 / Difference in x.
    int y_coords = 0;  // y の差 / Difference in y.
    // 詰め物。グリフは偶数や4の倍数の位置から始めるために、末尾が余る
    // Padding at the end, used to align glyphs to even or 4-byte boundaries.
    int padding = 0;  
};

// 今の文字について求めておいた数字
// Values already calculated for the current glyph.
struct GlyphData {
    uint32_t char_code = 0;
    int glyph_index = 0;
    Glyph glyph;
    GlyphBytes bytes;
    int on_count = 0;  // ファイルに書かれている、曲線の上の点 / On-curve point stored in the file.
    int off_count = 0;  // ファイルに書かれている、曲線の外の点 / Off-curve point stored in the file.
    int implied_count = 0;  // 描くときに補った上の点 / Added on-curve point used for drawing.
    float center_x = 0.0f;  // 輪郭の外側の四角の真ん中（フォントの単位） / Center of the outline's bounding box, in font units.
    float center_y = 0.0f;
};

// 輪郭の表示の状態
// Outline display state.
struct OutlineView {
    float em_pixels;  // 文字の大きさ（1 em）が何ピクセルか。拡大すると増える / Text size (1 em) in pixels. It grows with zoom.
    float center_x;  // 画面の真ん中に来る場所（フォントの単位） / Position in font units that appears at screen center.
    float center_y;
    bool show_points;
};

// 輪郭を画面のどこに置くか。上の説明の帯（29 ピクセル）と下の帯を除いた真ん中
// Where to place the outline on screen: in the middle area between the top explanation band (29 pixels) and the bottom band.
const float view_center_x = 160.0f;
const float view_center_y = 100.0f;
const float default_em_pixels = 130.0f;
const float min_em_pixels = 20.0f;
const float max_em_pixels = 8000.0f;  // これ以上は、線を引く回数が増えすぎる / More than this would require too many line segments.

// 0xRRGGBB の形の数を Color にする。お絵かきソフトのカラーコードと同じ並びで、左の2桁が赤、真ん中が緑、右の2桁が青
// Convert a number in 0xRRGGBB format to Color. It uses the same order as color codes in drawing software: the left two digits are red, the middle two green, and the right two blue.
Color from_hex(uint32_t hex) {
    const uint8_t r = static_cast<uint8_t>((hex >> 16) & 0xFF);
    const uint8_t g = static_cast<uint8_t>((hex >> 8) & 0xFF);
    const uint8_t b = static_cast<uint8_t>(hex & 0xFF);
    return Color{r, g, b};
}

// 色の明るさをずらす。選ばれていない印を、暗くするために使う
// Shift color brightness. Used to darken unselected markers.
Color shade(Color color, int delta) {
    return Color{
        static_cast<uint8_t>(std::clamp(color.r + delta, 0, 255)),
        static_cast<uint8_t>(std::clamp(color.g + delta, 0, 255)),
        static_cast<uint8_t>(std::clamp(color.b + delta, 0, 255)),
    };
}

// 四角形（x, y から幅 w、高さ h）を color で塗る。印や棒グラフを描くための道具
// Fill a rectangle at (x, y) with width w and height h using color. Helper for drawing markers and bars.
void fill_rect(Canvas& canvas, int x, int y, int w, int h, Color color) {
    for (int v = 0; v < h; v++) {
        for (int u = 0; u < w; u++) {
            set_pixel(canvas, x + u, y + v, color);
        }
    }
}

// 文字列を描いて、次の文字列を始める x を返す。色の違う文字列を横に並べるため
// Draw a string and return the x position for the next string. Used to place strings with different colors side by side.
int draw_text_next(Canvas& canvas, int x, int y, const char* text, Color color) {
    draw_text(canvas, x, y, text, color, 1);
    return x + text_char_width * static_cast<int>(std::strlen(text));
}

// 表の種類の色
// Color for the table type.
Color category_color(int category) {
    return from_hex(category_hexes[category]);
}

// 表のタグが name と同じか
// Whether the table tag matches name.
bool is_tag(const char* tag, const char* name) {
    return std::strcmp(tag, name) == 0;
}

// 表のタグから、どの種類の表かを返す
// Return the table type from its tag.
int table_category(const char* tag) {
    if (is_tag(tag, "glyf") || is_tag(tag, "loca")) {
        return category_outline;
    }
    if (is_tag(tag, "EBDT") || is_tag(tag, "EBLC") || is_tag(tag, "EBSC")) {
        return category_bitmap;
    }
    if (is_tag(tag, "fvar") || is_tag(tag, "gvar") || is_tag(tag, "avar") || is_tag(tag, "cvar") ||
        is_tag(tag, "HVAR") || is_tag(tag, "MVAR") || is_tag(tag, "VVAR")) {
        return category_variable;
    }
    // "cvt " は、4文字にそろえるために、最後に空白が入っている
    // A space pads "cvt " to four characters.
    if (is_tag(tag, "fpgm") || is_tag(tag, "prep") || is_tag(tag, "cvt ")) {
        return category_hint;
    }
    return category_other;
}

// パス "C:/Windows/Fonts/arial.ttf" から、最後の "arial.ttf" だけを取り出す
// Get only "arial.ttf" from the path "C:/Windows/Fonts/arial.ttf".
const char* file_name_of(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return (slash != nullptr) ? slash + 1 : path;
}

// ファイルがあるか。font.cpp の read_file は、開けないと「cannot open」と出すので、先にこちらで確かめる
// Whether the file exists. font.cpp read_file prints "cannot open" if it cannot open the file, so check here first.
bool file_exists(const char* path) {
    std::ifstream file(path, std::ios::binary);
    return file.is_open();
}

// 目次の大きい順に並べるための比べ方。同じ大きさなら、名前の順
// Comparison for sorting by directory offset. If equal, sort by name.
bool is_larger(const TableInfo& a, const TableInfo& b) {
    if (a.length != b.length) {
        return a.length > b.length;
    }
    return std::strcmp(a.tag, b.tag) < 0;
}

// 目次が directory_offset にあるフォントの、表の一覧を読む。font.cpp の find_table と同じ読み方（目次の1件は16バイト）
// Read the table list for the font whose directory is at directory_offset. Same parsing as find_table in font.cpp (each directory entry is 16 bytes).
void read_table_list(const std::vector<uint8_t>& bytes, size_t directory_offset, std::vector<TableInfo>& tables) {
    if (directory_offset + 12 > bytes.size()) {
        return;
    }

    const int table_count = read_u16(bytes, directory_offset + 4);
    for (int i = 0; i < table_count; i++) {
        const size_t record = directory_offset + 12 + 16 * static_cast<size_t>(i);
        if (record + 16 > bytes.size()) {
            break;
        }

        TableInfo table;
        for (size_t k = 0; k < 4; k++) {
            table.tag[k] = static_cast<char>(bytes[record + k]);
        }
        table.tag[4] = '\0';
        table.offset = read_u32(bytes, record + 8);
        table.length = read_u32(bytes, record + 12);
        tables.push_back(table);
    }
}

// 目次が directory_offset にあるフォントから、tag の表の位置と長さを探す。見つかれば true
// Find the table offset and length for tag in the font whose directory is at directory_offset. Return true if found.
bool find_table_range(const std::vector<uint8_t>& bytes, size_t directory_offset, const char* tag, size_t& offset, size_t& length) {
    std::vector<TableInfo> tables;
    read_table_list(bytes, directory_offset, tables);

    for (size_t i = 0; i < tables.size(); i++) {
        if (is_tag(tables[i].tag, tag)) {
            offset = tables[i].offset;
            length = tables[i].length;
            // 表がファイルの外を指していたら、壊れたファイルとして扱う
            // Treat the file as damaged if a table points outside it.
            return offset + length <= bytes.size();
        }
    }
    return false;
}

// name 表から、フォントの名前（nameID 4：フルネーム）を英語で読む。読めたら true
// name 表は、見出しのあとに、名前1件ごとの記録（12バイト）が並び、文字そのものは後ろの文字の置き場にまとまっている
//   記録：platformID、encodingID、languageID、nameID、長さ、文字の置き場の中での位置（どれも2バイト）
// Windows（platformID 3）の文字は UTF-16 で、英語は languageID 0x0409。Mac（platformID 1）の英語は 1バイトの文字
// Read the font name in English from the name table (nameID 4: full name). Return true if found.
// After the header, the name table has one 12-byte record per name. The actual text is stored together in a string area later in the table.
// Record fields: platformID, encodingID, languageID, nameID, length, and offset in the string area (2 bytes each).
// Windows text (platformID 3) uses UTF-16; English uses languageID 0x0409. Mac English (platformID 1) uses 1-byte text.
bool read_font_name(const std::vector<uint8_t>& bytes, size_t directory_offset, char* name, int name_size) {
    name[0] = '\0';

    size_t table = 0;
    size_t length = 0;
    if (!find_table_range(bytes, directory_offset, "name", table, length) || length < 6) {
        return false;
    }

    const int record_count = read_u16(bytes, table + 2);
    const size_t strings = table + read_u16(bytes, table + 4);
    for (int i = 0; i < record_count; i++) {
        const size_t record = table + 6 + 12 * static_cast<size_t>(i);
        if (record + 12 > table + length) {
            break;
        }

        const int platform_id = read_u16(bytes, record);
        const int language_id = read_u16(bytes, record + 4);
        const int name_id = read_u16(bytes, record + 6);
        const size_t text_length = read_u16(bytes, record + 8);
        const size_t text_offset = strings + read_u16(bytes, record + 10);

        const bool is_windows_english = (platform_id == 3 && language_id == 0x0409);
        const bool is_mac_english = (platform_id == 1 && language_id == 0);
        if (name_id != 4 || !(is_windows_english || is_mac_english) || text_offset + text_length > bytes.size()) {
            continue;
        }

        // 画面に出せるのは ASCII の文字だけ。それ以外は '?' にする。UTF-16 は、上のバイトが 0 のものだけ読む
        // Only ASCII characters can be shown on screen; replace other characters with '?'. Read UTF-16 only when the high byte is 0.
        const size_t step = is_windows_english ? 2 : 1;
        int count = 0;
        for (size_t k = 0; k + step <= text_length && count < name_size - 1; k += step) {
            const uint8_t high = (step == 2) ? bytes[text_offset + k] : 0;
            const uint8_t low = bytes[text_offset + k + step - 1];
            name[count] = (high == 0 && low >= 0x20 && low < 0x7F) ? static_cast<char>(low) : '?';
            count++;
        }
        name[count] = '\0';
        return count > 0;
    }
    return false;
}

// ファイルに入っているフォントの数と、それぞれの名前、表の数、共有している表を調べる
// TTC は、1つのファイルに複数のフォントを入れたもの。大きい表（glyf など）を、フォント同士で共有して、ファイルを小さくしている
// Inspect the number and names of fonts in the file, their table counts, and shared tables.
// A TTC stores multiple fonts in one file. Fonts share large tables such as glyf to reduce file size.
void read_members(FontData& data) {
    const std::vector<uint8_t>& bytes = data.font.data;

    // 先頭が "ttcf" なら TTC。0x74 0x74 0x63 0x66 を、4バイトの整数として読んだ値と比べる
    // If the first tag is "ttcf", this is a TTC. Compare it with 0x74746366 read as a 4-byte integer.
    data.is_ttc = (bytes.size() >= 16 && read_u32(bytes, 0) == 0x74746366);
    data.member_count = 1;
    if (data.is_ttc) {
        data.member_count = static_cast<int>(read_u32(bytes, 8));
    }

    const int shown = std::min(data.member_count, max_members);
    std::vector<std::vector<TableInfo>> member_tables;
    for (int i = 0; i < shown; i++) {
        // TTC の先頭には、各フォントの目次の位置が並んでいる（12 バイト目から、4バイトずつ）。TTC でなければ、目次はファイルの先頭
        // A TTC starts with the table-directory offset for each font (4 bytes each, starting at byte 12). For a non-TTC file, the directory starts at the beginning.
        size_t directory_offset = 0;
        if (data.is_ttc) {
            const size_t entry = 12 + 4 * static_cast<size_t>(i);
            if (entry + 4 > bytes.size()) {
                break;
            }
            directory_offset = read_u32(bytes, entry);
        }

        std::vector<TableInfo> tables;
        read_table_list(bytes, directory_offset, tables);

        MemberInfo member = {};
        read_font_name(bytes, directory_offset, member.name, static_cast<int>(sizeof(member.name)));
        member.table_count = static_cast<int>(tables.size());
        for (size_t t = 0; t < tables.size(); t++) {
            member.total_bytes += tables[t].length;
            if (is_tag(tables[t].tag, "glyf")) {
                member.glyf_offset = tables[t].offset;
            }

            // 先に出てきたフォントに、同じ位置・同じ長さの表があれば、共有している
            // A table is shared if an earlier font has a table at the same offset and with the same length.
            bool is_shared = false;
            for (int j = 0; j < i; j++) {
                for (size_t u = 0; u < member_tables[j].size(); u++) {
                    if (member_tables[j][u].offset == tables[t].offset && member_tables[j][u].length == tables[t].length) {
                        is_shared = true;
                    }
                }
            }
            if (is_shared) {
                member.shared_count++;
            } else {
                member.own_bytes += tables[t].length;
            }
        }

        data.members.push_back(member);
        member_tables.push_back(tables);
    }
}

// EBLC（ドット絵の目次）から、ドット絵のサイズの数と、一番小さいサイズ・大きいサイズを読む
// EBLC の頭：0: バージョン（4）、4: サイズの数（4）。そのあとに、サイズごとの記録（48バイト）が並ぶ。記録の44バイト目が、文字の高さ（ピクセル）
// Read the number of bitmap sizes and the smallest and largest sizes from EBLC (the bitmap index).
// EBLC header: 0: version (4 bytes), 4: number of sizes (4 bytes). This is followed by 48-byte records, one per size. Byte 44 of each record is the glyph height in pixels.
void read_bitmap_sizes(FontData& data) {
    const std::vector<uint8_t>& bytes = data.font.data;
    size_t table = 0;
    size_t length = 0;
    if (!find_table_range(bytes, data.font.directory_offset, "EBLC", table, length) || length < 8) {
        return;
    }

    const int size_count = static_cast<int>(read_u32(bytes, table + 4));
    for (int i = 0; i < size_count; i++) {
        const size_t record = table + 8 + 48 * static_cast<size_t>(i);
        if (record + 48 > table + length) {
            break;
        }

        const int ppem = bytes[record + 44];
        if (data.bitmap_size_count == 0 || ppem < data.bitmap_min_ppem) {
            data.bitmap_min_ppem = ppem;
        }
        if (data.bitmap_size_count == 0 || ppem > data.bitmap_max_ppem) {
            data.bitmap_max_ppem = ppem;
        }
        data.bitmap_size_count++;
    }
}

// fvar（可変フォントの軸）から、軸の数と、最初の軸の名前・最小・初期値・最大を読む
// fvar の頭：4: 軸の記録の位置（2）、8: 軸の数（2）、10: 軸の記録1つの大きさ（2）
// 軸の記録：0: 名前（4文字。"wght" は太さ）、4: 最小、8: 初期値、12: 最大（どれも、1 が 65536 の小数）
// Read the number of axes and the first axis name, minimum, default, and maximum from fvar (variable-font axes).
// fvar header: 4: axis-record offset (2 bytes), 8: axis count (2), 10: size of each axis record (2).
// Axis record: 0: 4-character name ("wght" is weight), 4: minimum, 8: default, 12: maximum (each is a fixed-point value where 1 is 65536).
void read_variable_axis(FontData& data) {
    const std::vector<uint8_t>& bytes = data.font.data;
    size_t table = 0;
    size_t length = 0;
    if (!find_table_range(bytes, data.font.directory_offset, "fvar", table, length) || length < 16) {
        return;
    }

    data.axis_count = read_u16(bytes, table + 8);
    const size_t axis = table + read_u16(bytes, table + 4);
    if (data.axis_count == 0 || axis + 20 > table + length) {
        return;
    }

    for (size_t k = 0; k < 4; k++) {
        data.axis_tag[k] = static_cast<char>(bytes[axis + k]);
    }
    data.axis_tag[4] = '\0';
    data.axis_min = static_cast<float>(static_cast<int32_t>(read_u32(bytes, axis + 4))) / 65536.0f;
    data.axis_default = static_cast<float>(static_cast<int32_t>(read_u32(bytes, axis + 8))) / 65536.0f;
    data.axis_max = static_cast<float>(static_cast<int32_t>(read_u32(bytes, axis + 12))) / 65536.0f;
}

// フォントを読んで、表示に使う数字を求める。毎フレームではなく、フォントを切り替えたときだけ呼ぶ
// Read the font and collect values for display. Call this only when switching fonts, not every frame.
void load_font_data(FontData& data, const char* path) {
    data = FontData();

    if (!file_exists(path)) {
        data.status = font_not_found;
        return;
    }
    if (!load_font(data.font, path)) {
        data.status = font_unreadable;
        return;
    }
    data.status = font_ok;

    read_table_list(data.font.data, data.font.directory_offset, data.tables);
    for (size_t i = 0; i < data.tables.size(); i++) {
        data.category_bytes[table_category(data.tables[i].tag)] += data.tables[i].length;
    }
    std::sort(data.tables.begin(), data.tables.end(), is_larger);

    read_members(data);
    read_bitmap_sizes(data);
    read_variable_axis(data);
}

// グリフ1つのデータが、何バイトで、何にどれだけ使われているかを数える
// load_glyph と同じ順に読み進めて、読んだ部分ごとにバイト数を足していく
// Count the bytes used by each part of one glyph.
// Read the data in the same order as load_glyph and add the byte count for each part.
GlyphBytes measure_glyph(const Font& font, int glyph_index) {
    GlyphBytes bytes;
    const std::vector<uint8_t>& data = font.data;

    if (glyph_index < 0 || glyph_index >= font.num_glyphs) {
        return bytes;
    }

    // loca から、このグリフの始まりと終わりを求める。load_glyph と同じ
    // Find this glyph's start and end in loca. Same as load_glyph.
    const size_t i = static_cast<size_t>(glyph_index);
    size_t begin = 0;
    size_t end = 0;
    if (font.index_to_loc_format == 0) {
        begin = static_cast<size_t>(read_u16(data, font.loca_offset + 2 * i)) * 2;
        end = static_cast<size_t>(read_u16(data, font.loca_offset + 2 * (i + 1))) * 2;
    } else {
        begin = read_u32(data, font.loca_offset + 4 * i);
        end = read_u32(data, font.loca_offset + 4 * (i + 1));
    }
    if (end <= begin || font.glyf_offset + end > data.size()) {
        return bytes;
    }

    bytes.total = static_cast<int>(end - begin);
    bytes.header = 10;

    const size_t start = font.glyf_offset + begin;
    const size_t glyph_end = font.glyf_offset + end;
    const int contour_count = read_i16(data, start);
    if (contour_count < 0) {
        // 複合グリフは、内訳を数えない
        // Do not count the parts of composite glyphs.
        bytes.is_composite = true;
        return bytes;
    }
    if (contour_count == 0) {
        return bytes;
    }

    // 輪郭ごとの最後の点の番号。最後の輪郭の最後の点の番号 + 1 が、点の数
    // Index of the last point in each contour. The last point index of the final contour plus 1 is the point count.
    bytes.contour_ends = 2 * contour_count;
    size_t p = start + 10;
    const int point_count = read_u16(data, p + 2 * static_cast<size_t>(contour_count - 1)) + 1;
    p += static_cast<size_t>(bytes.contour_ends);

    // ヒンティングの命令：長さを書く2バイトと、命令そのもの
    // Hinting instructions: 2 bytes for the length, followed by the instructions.
    bytes.instruction_size = 2;
    bytes.instructions = read_u16(data, p);
    p += 2 + static_cast<size_t>(bytes.instructions);

    // フラグ。点の数だけ並び、繰り返しの印があれば、次の1バイトの回数だけ同じフラグが続く
    // Flags, one per point. If the repeat bit is set, the next byte gives the number of additional copies of that flag.
    std::vector<uint8_t> flags;
    while (static_cast<int>(flags.size()) < point_count && p < glyph_end) {
        const uint8_t flag = data[p];
        p++;
        bytes.flags++;
        flags.push_back(flag);
        if ((flag & flag_repeat) && p < glyph_end) {
            const int repeat_count = data[p];
            p++;
            bytes.flags++;
            for (int r = 0; r < repeat_count; r++) {
                flags.push_back(flag);
            }
        }
    }

    // x と y の差。フラグが、1バイトか、2バイトか、何も読まないか（前の点と同じ）を決める
    // Difference in x and y. Flags say whether to read 1 byte, 2 bytes, or nothing (same as the previous point).
    for (size_t k = 0; k < flags.size(); k++) {
        if (flags[k] & flag_x_short) {
            bytes.x_coords += 1;
        } else if (!(flags[k] & flag_x_same_or_positive)) {
            bytes.x_coords += 2;
        }
        if (flags[k] & flag_y_short) {
            bytes.y_coords += 1;
        } else if (!(flags[k] & flag_y_same_or_positive)) {
            bytes.y_coords += 2;
        }
    }

    // 数えた分を引いた残りが詰め物
    // The remaining bytes after counted data are padding.
    const int counted = bytes.header + bytes.contour_ends + bytes.instruction_size + bytes.instructions + bytes.flags + bytes.x_coords + bytes.y_coords;
    bytes.padding = bytes.total - counted;
    return bytes;
}

// 文字から、グリフを読んで、点の数やバイト数を求める。フォントか文字を切り替えたときだけ呼ぶ
// Read glyphs for the text and find point counts and byte sizes. Call only when switching fonts or text.
void load_glyph_data(GlyphData& glyph_data, const FontData& data, uint32_t char_code) {
    glyph_data = GlyphData();
    glyph_data.char_code = char_code;
    if (data.status != font_ok) {
        return;
    }

    glyph_data.glyph_index = find_glyph_index(data.font, char_code);
    glyph_data.glyph = load_glyph(data.font, glyph_data.glyph_index);
    glyph_data.bytes = measure_glyph(data.font, glyph_data.glyph_index);

    // ファイルに書かれている点の数。上の点と外の点
    // Number of points stored in the file, including on-curve and off-curve points.
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;
    const std::vector<Point>& points = glyph_data.glyph.points;
    for (size_t i = 0; i < points.size(); i++) {
        if (points[i].on_curve) {
            glyph_data.on_count++;
        } else {
            glyph_data.off_count++;
        }
        if (i == 0 || points[i].x < min_x) {
            min_x = points[i].x;
        }
        if (i == 0 || points[i].x > max_x) {
            max_x = points[i].x;
        }
        if (i == 0 || points[i].y < min_y) {
            min_y = points[i].y;
        }
        if (i == 0 || points[i].y > max_y) {
            max_y = points[i].y;
        }
    }

    // 補った点の数。描くときと同じ並べ直しをして、補った点だけ数える
    // Number of added points. Recreate the drawing point sequence and count only added points.
    const std::vector<std::vector<OutlinePoint>> contours = make_outline(glyph_data.glyph, 0.0f, 0.0f, 1.0f);
    for (size_t c = 0; c < contours.size(); c++) {
        for (size_t i = 0; i < contours[c].size(); i++) {
            if (contours[c][i].implied) {
                glyph_data.implied_count++;
            }
        }
    }

    // 点がなければ、1 em の四角の真ん中あたりを見る
    // If there are no points, view near the center of the 1 em square.
    if (points.empty()) {
        glyph_data.center_x = static_cast<float>(data.font.units_per_em) * 0.5f;
        glyph_data.center_y = static_cast<float>(data.font.units_per_em) * 0.35f;
    } else {
        glyph_data.center_x = static_cast<float>(min_x + max_x) * 0.5f;
        glyph_data.center_y = static_cast<float>(min_y + max_y) * 0.5f;
    }
}

// 文字を「U+0041 'A'」のような文字列にする。日本語の文字は draw_text で出せないので、文字コードで書く
// Format a character as text such as "U+0041 'A'". draw_text cannot show Japanese characters, so use code points.
void describe_char(uint32_t char_code, char* text, int text_size) {
    if (char_code >= 0x20 && char_code < 0x7F) {
        std::snprintf(text, static_cast<size_t>(text_size), "U+%04X '%c'", static_cast<unsigned int>(char_code), static_cast<char>(char_code));
    } else {
        std::snprintf(text, static_cast<size_t>(text_size), "U+%04X", static_cast<unsigned int>(char_code));
    }
}

// 部分 part が全体 whole の何パーセントか
// What percentage part is of the whole.
double percent_of(double part, double whole) {
    return (whole > 0.0) ? 100.0 * part / whole : 0.0;
}

// キーが押された瞬間だけ true を返す。押しっぱなしでは1回しか反応しない
// Return true only on the frame when the key is pressed. Holding it down does not trigger it again.
bool key_pressed(Canvas& canvas, int key, bool was_down[]) {
    const bool is_down = canvas.is_key_down(key);
    const bool pressed = is_down && !was_down[key];
    was_down[key] = is_down;
    return pressed;
}

// 棒を、values の割合で色分けして描く。total が棒の全体の長さに当たる値
// 小さい値でも、0 でなければ最低 1 ピクセルは塗る。そうしないと、小さい部分が消えて、あることに気づけない
// 棒の背景（values に入らない残りの部分）は、暗い色のまま残る
// Draw the bar with segments sized by values. total is the value for the full bar length.
// For any nonzero value, fill at least 1 pixel so small parts do not disappear.
// Leave the unused part in a dark background color.
void draw_bar(Canvas& canvas, int x, int y, int width, int height, const double* values, const Color* colors, int count, double total) {
    fill_rect(canvas, x, y, width, height, Color{40, 40, 72});

    double sum = 0.0;
    int left = 0;
    for (int k = 0; k < count; k++) {
        sum += values[k];
        int right = static_cast<int>(std::round(static_cast<double>(width) * sum / total));
        if (values[k] > 0.0 && right <= left) {
            right = left + 1;
        }
        right = std::min(right, width);
        fill_rect(canvas, x + left, y, right - left, height, colors[k]);
        left = right;
    }
}

// 凡例を1つ描く。色の四角と、文字
// Draw one legend entry: a color square and its label.
void draw_legend_item(Canvas& canvas, int x, int y, Color color, const char* text) {
    fill_rect(canvas, x, y + 1, 6, 6, color);
    draw_text(canvas, x + 10, y, text, Color{255, 255, 255}, 1);
}

// 画面の下の印を描く。左の4つが今の表示、右の6つが今のフォント。選ばれているものだけ明るい
// Draw the markers at the bottom. The left four show the current view; the right six show the font. Brighten only the selected item.
void draw_indicators(Canvas& canvas, int view, int font_number) {
    const uint32_t view_hexes[view_count] = {0x40E040, 0xFF9020, 0xC060FF, 0xFFE040};
    const uint32_t font_hexes[font_entry_count] = {0x40D0FF, 0xFF60C0, 0xFFA030, 0xA0FF60, 0xC080FF, 0xE0E0E0};

    for (int k = 0; k < view_count; k++) {
        const Color color = (k == view) ? from_hex(view_hexes[k]) : shade(from_hex(view_hexes[k]), -170);
        fill_rect(canvas, 4 + k * 10, 176, 8, 3, color);
    }
    for (int k = 0; k < font_entry_count; k++) {
        const Color color = (k == font_number) ? from_hex(font_hexes[k]) : shade(from_hex(font_hexes[k]), -170);
        fill_rect(canvas, 56 + k * 10, 176, 8, 3, color);
    }
}

// フォントが読めなかったときの表示。どの表示でも、これを出す
// Message shown when the font could not be loaded. Use it in every view.
void draw_unavailable(Canvas& canvas, const FontEntry& entry, const FontData& data) {
    const Color white = {255, 255, 255};
    const Color red = {255, 96, 96};
    const Color gray = {150, 150, 170};

    draw_text(canvas, 4, 4, entry.label, white, 1);
    if (data.status == font_not_found) {
        draw_text(canvas, 4, 22, "not found", red, 2);
        draw_text(canvas, 4, 44, "this font is not installed:", gray, 1);
    } else {
        draw_text(canvas, 4, 22, "cannot read", red, 2);
        draw_text(canvas, 4, 44, "this program cannot read:", gray, 1);
    }
    draw_text(canvas, 4, 53, entry.path, white, 1);
}

// 輪郭の表示。大きく拡大しても、輪郭はくっきりしている。入っているのは点と曲線の数字だけなので、描くときに好きな大きさにできる
// Outline display. The outline stays sharp when enlarged because it stores only points and curve data, so it can be drawn at any size.
void draw_outline_view(Canvas& canvas, const FontEntry& entry, const FontData& data, const GlyphData& glyph_data, const OutlineView& view) {
    const Color white = {255, 255, 255};
    const Color gray = {150, 150, 170};
    const Color green = {64, 255, 64};
    const Color red = {255, 64, 64};
    const Color light_blue = {64, 200, 255};
    const Color yellow = from_hex(0xFFE040);
    const Color panel = {32, 32, 80};

    // 輪郭。view.center が画面の真ん中に来るように、フォントの座標 (0, 0) を置く場所を決める
    // Outline view. Place font coordinate (0, 0) so view.center appears at screen center.
    const float scale = view.em_pixels / static_cast<float>(data.font.units_per_em);
    const float origin_x = view_center_x - view.center_x * scale;
    const float baseline_y = view_center_y + view.center_y * scale;
    draw_glyph_outline(canvas, glyph_data.glyph, origin_x, baseline_y, scale, white);
    if (view.show_points) {
        draw_glyph_points(canvas, glyph_data.glyph, origin_x, baseline_y, scale);
    }

    // 説明の帯。拡大した輪郭が重なっても、文字が読めるように、背景を塗る
    // Label strip. Fill its background so text stays readable over magnified outlines.
    fill_rect(canvas, 0, 0, canvas.width, 29, panel);
    fill_rect(canvas, 0, 167, canvas.width, 13, panel);

    char char_text[32];
    describe_char(glyph_data.char_code, char_text, static_cast<int>(sizeof(char_text)));
    char line[96];
    std::snprintf(line, sizeof(line), "%s  %s  glyph %d%s", entry.label, char_text, glyph_data.glyph_index,
        (glyph_data.glyph_index == 0) ? " (not in font)" : "");
    draw_text(canvas, 4, 2, line, white, 1);

    // 点の数。色は、輪郭に重ねる点と同じ
    // Number of points. Use the same colors as the points overlaid on the outline.
    const int point_total = static_cast<int>(glyph_data.glyph.points.size());
    std::snprintf(line, sizeof(line), "points %d", point_total);
    int x = draw_text_next(canvas, 4, 11, line, white);
    std::snprintf(line, sizeof(line), "  on %d", glyph_data.on_count);
    x = draw_text_next(canvas, x, 11, line, green);
    std::snprintf(line, sizeof(line), "  off %d", glyph_data.off_count);
    x = draw_text_next(canvas, x, 11, line, red);
    std::snprintf(line, sizeof(line), "  implied %d", glyph_data.implied_count);
    draw_text_next(canvas, x, 11, line, light_blue);

    // グリフのデータの大きさ
    // Size of the glyph data.
    const GlyphBytes& bytes = glyph_data.bytes;
    if (bytes.is_composite) {
        std::snprintf(line, sizeof(line), "composite glyph, %d bytes (not drawn)", bytes.total);
        draw_text(canvas, 4, 20, line, gray, 1);
    } else {
        std::snprintf(line, sizeof(line), "glyph data %d bytes, hint", bytes.total);
        x = draw_text_next(canvas, 4, 20, line, white);
        std::snprintf(line, sizeof(line), " %d bytes", bytes.instructions);
        draw_text_next(canvas, x, 20, line, yellow);
    }

    // 下の帯：点を出しているか、拡大の倍率
    // Bottom strip: whether points are shown and the zoom level.
    std::snprintf(line, sizeof(line), "points %s  x%.1f  1 em = %.0f px", view.show_points ? "on" : "off",
        static_cast<double>(view.em_pixels / default_em_pixels), static_cast<double>(view.em_pixels));
    draw_text(canvas, canvas.width - 4 - text_char_width * static_cast<int>(std::strlen(line)), 168, line, gray, 1);
}

// 表の一覧。バイト数の大きい順に並べて、種類ごとに色分けする
// Table list, sorted by byte size and colored by type.
void draw_tables_view(Canvas& canvas, const FontEntry& entry, const FontData& data) {
    const Color white = {255, 255, 255};
    const Color gray = {150, 150, 170};
    char line[96];

    std::snprintf(line, sizeof(line), "%s  %s", entry.label, file_name_of(entry.path));
    draw_text(canvas, 4, 2, line, white, 1);
    const double file_size = static_cast<double>(data.font.data.size());
    std::snprintf(line, sizeof(line), "file %zu bytes, %d tables", data.font.data.size(), data.font.num_tables);
    draw_text(canvas, 4, 11, line, gray, 1);

    // ファイル全体を1本の棒にして、種類ごとの割合を見せる。残りは暗いまま
    // Show the whole file as one bar, with the share for each type. Keep the rest dark.
    double values[4];
    Color colors[4];
    for (int k = 0; k < 4; k++) {
        values[k] = static_cast<double>(data.category_bytes[k]);
        colors[k] = category_color(k);
    }
    draw_bar(canvas, 4, 21, canvas.width - 8, 7, values, colors, 4, file_size);

    // 表を、大きい順に、左の列から並べる
    // Arrange tables from largest to smallest, starting in the left column.
    const int rows_per_column = 12;
    const int shown = std::min(static_cast<int>(data.tables.size()), rows_per_column * 2);
    for (int i = 0; i < shown; i++) {
        const TableInfo& table = data.tables[static_cast<size_t>(i)];
        const int x = (i / rows_per_column == 0) ? 4 : 164;
        const int y = 30 + (i % rows_per_column) * text_line_height;
        std::snprintf(line, sizeof(line), "%-4s %8u %4.1f%%", table.tag, table.length, percent_of(static_cast<double>(table.length), file_size));
        draw_text(canvas, x, y, line, category_color(table_category(table.tag)), 1);
    }

    // 種類ごとの割合
    // Share by type.
    for (int k = 0; k < 4; k++) {
        std::snprintf(line, sizeof(line), "%s %.2f%%", category_names[k], percent_of(values[k], file_size));
        draw_legend_item(canvas, (k % 2 == 0) ? 4 : 164, 141 + (k / 2) * 9, colors[k], line);
    }

    // ドット絵と可変フォントの、くわしい数字
    // Details for bitmap glyphs and variable fonts.
    int y = 159;
    if (data.bitmap_size_count > 0) {
        std::snprintf(line, sizeof(line), "bitmaps: %d sizes, %d to %d px", data.bitmap_size_count, data.bitmap_min_ppem, data.bitmap_max_ppem);
        draw_text(canvas, 4, y, line, category_color(category_bitmap), 1);
        y += 9;
    }
    if (data.axis_count > 0) {
        std::snprintf(line, sizeof(line), "variable: %s %.0f to %.0f (glyf is %.0f)", data.axis_tag,
            static_cast<double>(data.axis_min), static_cast<double>(data.axis_max), static_cast<double>(data.axis_default));
        draw_text(canvas, 4, y, line, category_color(category_variable), 1);
    }
}

// TTC の中身。ファイルに入っているフォントの数と、それぞれの名前
// TTC contents: number of fonts in the file and each font name.
void draw_members_view(Canvas& canvas, const FontEntry& entry, const FontData& data) {
    const Color white = {255, 255, 255};
    const Color gray = {150, 150, 170};
    const Color green = category_color(category_outline);
    char line[96];

    std::snprintf(line, sizeof(line), "%s  %s", entry.label, file_name_of(entry.path));
    draw_text(canvas, 4, 2, line, white, 1);
    if (data.is_ttc) {
        std::snprintf(line, sizeof(line), "%zu bytes, TTC with %d fonts", data.font.data.size(), data.member_count);
    } else {
        std::snprintf(line, sizeof(line), "%zu bytes, not a TTC (1 font)", data.font.data.size());
    }
    draw_text(canvas, 4, 11, line, gray, 1);

    // フォントごとに2行。名前と、表の数・共有している表の数・共有していない表のバイト数
    // Two lines per font: name, table count, number of shared tables, and bytes in unshared tables.
    int y = 24;
    uint32_t separate_bytes = 0;
    bool glyf_is_shared = (data.members.size() > 1);
    for (size_t i = 0; i < data.members.size(); i++) {
        const MemberInfo& member = data.members[i];
        std::snprintf(line, sizeof(line), "#%d %s", static_cast<int>(i), (member.name[0] != '\0') ? member.name : "(name unreadable)");
        draw_text(canvas, 4, y, line, white, 1);
        std::snprintf(line, sizeof(line), "   %d tables, %d shared, own %u", member.table_count, member.shared_count, member.own_bytes);
        draw_text(canvas, 4, y + 9, line, gray, 1);
        y += 20;

        separate_bytes += member.total_bytes;
        if (member.glyf_offset != data.members[0].glyf_offset) {
            glyf_is_shared = false;
        }
    }

    // 別々のファイルにしたら、どれだけ大きくなるか。TTC は、共有する表を1つだけ持って、ファイルを小さくしている
    // How large would the fonts be as separate files? A TTC saves space by storing shared tables only once.
    if (data.is_ttc) {
        y += 4;
        std::snprintf(line, sizeof(line), "as %d separate files:", static_cast<int>(data.members.size()));
        draw_text(canvas, 4, y, line, gray, 1);
        std::snprintf(line, sizeof(line), "%u bytes", separate_bytes);
        draw_text(canvas, 4, y + 9, line, white, 1);
        std::snprintf(line, sizeof(line), "this TTC file:  %zu bytes", data.font.data.size());
        draw_text(canvas, 4, y + 18, line, green, 1);
        draw_text(canvas, 4, y + 27, glyf_is_shared ? "glyf is stored once and shared" : "glyf is not shared", glyf_is_shared ? green : gray, 1);
    }
}

// 1文字のデータの内訳。グリフのデータが何バイトで、何にどれだけ使われているか
// Contents of one character: how many bytes of glyph data are used for each part.
void draw_bytes_view(Canvas& canvas, const FontEntry& entry, const GlyphData& glyph_data) {
    const Color white = {255, 255, 255};
    const Color gray = {150, 150, 170};
    char line[96];

    char char_text[32];
    describe_char(glyph_data.char_code, char_text, static_cast<int>(sizeof(char_text)));
    std::snprintf(line, sizeof(line), "%s  %s  glyph %d%s", entry.label, char_text, glyph_data.glyph_index,
        (glyph_data.glyph_index == 0) ? " (not in font)" : "");
    draw_text(canvas, 4, 2, line, white, 1);

    const GlyphBytes& bytes = glyph_data.bytes;
    if (bytes.total == 0) {
        draw_text(canvas, 4, 11, "no data (the glyph is empty)", gray, 1);
        return;
    }
    if (bytes.is_composite) {
        std::snprintf(line, sizeof(line), "%d bytes. a composite glyph:", bytes.total);
        draw_text(canvas, 4, 11, line, gray, 1);
        draw_text(canvas, 4, 20, "it only lists other glyphs", gray, 1);
        return;
    }

    std::snprintf(line, sizeof(line), "%d bytes for %d points", bytes.total, static_cast<int>(glyph_data.glyph.points.size()));
    draw_text(canvas, 4, 11, line, gray, 1);

    // 内訳。名前、バイト数、全体に対する割合。ヒンティングの命令は黄色
    // Breakdown: name, byte count, and share of the total. Hinting instructions are yellow.
    const char* names[8] = {"header", "contour ends", "hint size", "hint program", "flags", "x coords", "y coords", "padding"};
    const int sizes[8] = {bytes.header, bytes.contour_ends, bytes.instruction_size, bytes.instructions, bytes.flags, bytes.x_coords, bytes.y_coords, bytes.padding};
    const uint32_t hexes[8] = {0x8890B8, 0x60A8F0, 0xB8A030, 0xFFE040, 0x50DC8C, 0xFFA030, 0xFF70B0, 0x707088};

    double values[8];
    Color colors[8];
    for (int k = 0; k < 8; k++) {
        values[k] = static_cast<double>(std::max(sizes[k], 0));
        colors[k] = from_hex(hexes[k]);
    }
    draw_bar(canvas, 4, 22, canvas.width - 8, 9, values, colors, 8, static_cast<double>(bytes.total));

    for (int k = 0; k < 8; k++) {
        std::snprintf(line, sizeof(line), "%-12s %5d %5.1f%%", names[k], sizes[k], percent_of(static_cast<double>(sizes[k]), static_cast<double>(bytes.total)));
        draw_legend_item(canvas, 4, 38 + k * 10, colors[k], line);
    }

    std::snprintf(line, sizeof(line), "total        %5d", bytes.total);
    draw_text(canvas, 14, 38 + 8 * 10, line, white, 1);

    // 点の位置を表すのに使っているバイト数。フラグと x、y の差だけで、1点あたり数バイトしかない
    // Bytes used to store point positions. Each point takes only a few bytes for its flag and x and y deltas.
    const int point_count = static_cast<int>(glyph_data.glyph.points.size());
    if (point_count > 0) {
        const int point_bytes = bytes.flags + bytes.x_coords + bytes.y_coords;
        std::snprintf(line, sizeof(line), "flags + x + y: %d bytes", point_bytes);
        draw_text(canvas, 4, 134, line, gray, 1);
        std::snprintf(line, sizeof(line), "%.1f bytes per point", static_cast<double>(point_bytes) / static_cast<double>(point_count));
        draw_text(canvas, 4, 143, line, gray, 1);
    }
}

// 表示している内容を、コンソールにも出す
// Also print the current view information to the console.
void print_outline_report(const FontEntry& entry, const GlyphData& glyph_data) {
    char char_text[32];
    describe_char(glyph_data.char_code, char_text, static_cast<int>(sizeof(char_text)));
    const GlyphBytes& bytes = glyph_data.bytes;
    std::printf("outline: %s %s glyph %d, %d contours, %d points in the file (on %d, off %d), implied %d\n",
        entry.label, char_text, glyph_data.glyph_index, static_cast<int>(glyph_data.glyph.contour_ends.size()),
        static_cast<int>(glyph_data.glyph.points.size()), glyph_data.on_count, glyph_data.off_count, glyph_data.implied_count);
    if (bytes.is_composite) {
        std::printf("  glyph data: %d bytes (composite glyph)\n", bytes.total);
    } else {
        std::printf("  glyph data: %d bytes, hint %d bytes\n", bytes.total, bytes.instructions);
    }
}

void print_tables_report(const FontEntry& entry, const FontData& data) {
    const double file_size = static_cast<double>(data.font.data.size());
    std::printf("tables: %s (%s), file %zu bytes, %d tables\n", entry.label, file_name_of(entry.path), data.font.data.size(), data.font.num_tables);
    std::printf("  tag        bytes  of file  kind\n");
    for (size_t i = 0; i < data.tables.size(); i++) {
        const TableInfo& table = data.tables[i];
        const int category = table_category(table.tag);
        std::printf("  %-4s %10u  %6.2f%%  %s\n", table.tag, table.length,
            percent_of(static_cast<double>(table.length), file_size), (category == category_other) ? "" : category_names[category]);
    }
    for (int k = 0; k < 4; k++) {
        std::printf("  %-8s %6.2f%% (%u bytes)\n", category_names[k], percent_of(static_cast<double>(data.category_bytes[k]), file_size), data.category_bytes[k]);
    }
    if (data.bitmap_size_count > 0) {
        std::printf("  bitmaps: %d sizes, %d to %d px\n", data.bitmap_size_count, data.bitmap_min_ppem, data.bitmap_max_ppem);
    }
    if (data.axis_count > 0) {
        std::printf("  variable: %d axes, first axis %s %.0f to %.0f, default %.0f (the outlines in glyf are for the default)\n", data.axis_count, data.axis_tag,
            static_cast<double>(data.axis_min), static_cast<double>(data.axis_max), static_cast<double>(data.axis_default));
    }
}

void print_members_report(const FontEntry& entry, const FontData& data) {
    if (data.is_ttc) {
        std::printf("members: %s (%s), file %zu bytes, TTC with %d fonts\n", entry.label, file_name_of(entry.path), data.font.data.size(), data.member_count);
    } else {
        std::printf("members: %s (%s), file %zu bytes, not a TTC (1 font)\n", entry.label, file_name_of(entry.path), data.font.data.size());
    }
    uint32_t separate_bytes = 0;
    for (size_t i = 0; i < data.members.size(); i++) {
        const MemberInfo& member = data.members[i];
        std::printf("  #%d %s: %d tables, %d shared with an earlier font, own %u bytes, total %u bytes\n", static_cast<int>(i),
            (member.name[0] != '\0') ? member.name : "(name unreadable)", member.table_count, member.shared_count, member.own_bytes, member.total_bytes);
        separate_bytes += member.total_bytes;
    }
    if (data.is_ttc) {
        std::printf("  as separate files: %u bytes, this file: %zu bytes\n", separate_bytes, data.font.data.size());
    }
}

void print_bytes_report(const FontEntry& entry, const GlyphData& glyph_data) {
    char char_text[32];
    describe_char(glyph_data.char_code, char_text, static_cast<int>(sizeof(char_text)));
    const GlyphBytes& bytes = glyph_data.bytes;
    std::printf("glyph bytes: %s %s glyph %d, total %d bytes%s\n", entry.label, char_text, glyph_data.glyph_index, bytes.total, bytes.is_composite ? " (composite glyph)" : "");
    if (bytes.total > 0 && !bytes.is_composite) {
        std::printf("  header %d, contour ends %d, hint size %d, hint program %d, flags %d, x coords %d, y coords %d, padding %d\n",
            bytes.header, bytes.contour_ends, bytes.instruction_size, bytes.instructions, bytes.flags, bytes.x_coords, bytes.y_coords, bytes.padding);
    }
}

// 今の表示の内容を、コンソールに出す
// Print the current view information to the console.
void print_report(int view, const FontEntry& entry, const FontData& data, const GlyphData& glyph_data) {
    if (data.status == font_not_found) {
        std::printf("%s: not found (%s)\n", entry.label, entry.path);
        return;
    }
    if (data.status == font_unreadable) {
        std::printf("%s: cannot read (%s)\n", entry.label, entry.path);
        return;
    }

    if (view == view_outline) {
        print_outline_report(entry, glyph_data);
    } else if (view == view_tables) {
        print_tables_report(entry, data);
    } else if (view == view_members) {
        print_members_report(entry, data);
    } else {
        print_bytes_report(entry, glyph_data);
    }
}

int main() {
    Canvas canvas(320, 180, 4);  // 320×180 の絵を、4倍に拡大して表示する / Display a 320x180 image enlarged 4 times.

    const Color background = {16, 16, 48};

    int font_number = 0;  // font_entries の何番目か / Index in font_entries.
    int sample_number = 0;  // sample_chars の何番目か / Index in sample_chars.
    int view = view_outline;
    OutlineView outline_view = {default_em_pixels, 0.0f, 0.0f, true};
    const int view_keys[view_count] = {'O', 'T', 'M', 'G'};  // 表示を切り替えるキー / Key to switch views.
    bool was_down[256] = {};

    // フォントと文字は、切り替えたときだけ読む。毎フレーム読むと、十数MBのファイルを何度も読み直すことになる
    // Load fonts and glyphs only when switching. Reading them every frame would repeatedly load a file over 10 MB.
    FontData font_data;
    GlyphData glyph_data;
    load_font_data(font_data, font_entries[font_number].path);
    load_glyph_data(glyph_data, font_data, sample_chars[sample_number]);
    outline_view.center_x = glyph_data.center_x;
    outline_view.center_y = glyph_data.center_y;

    std::printf("1-6: font  C: next character  O: outline  T: tables  M: TTC members  G: glyph bytes\n");
    std::printf("P: points  Z/X: zoom in/out  arrows: move  R: reset the view  Esc: quit\n");
    print_report(view, font_entries[font_number], font_data, glyph_data);

    while (canvas.update()) {
        if (canvas.is_key_down(key_escape)) {
            break;
        }

        // 表示を変えたときだけ、コンソールに知らせる
        // Print to the console only when the view changes.
        bool is_changed = false;

        // フォントの切り替え。文字は同じまま、フォントだけ変える
        // Switch fonts while keeping the same text.
        for (int k = 0; k < font_entry_count; k++) {
            if (key_pressed(canvas, '1' + k, was_down) && k != font_number) {
                font_number = k;
                load_font_data(font_data, font_entries[font_number].path);
                load_glyph_data(glyph_data, font_data, sample_chars[sample_number]);
                outline_view.center_x = glyph_data.center_x;
                outline_view.center_y = glyph_data.center_y;
                is_changed = true;
            }
        }

        // 文字の切り替え。拡大はそのままで、新しい文字の真ん中に見る場所を移す
        // Switch glyphs. Keep the zoom and move the view to the center of the new glyph.
        if (key_pressed(canvas, 'C', was_down)) {
            sample_number = (sample_number + 1) % sample_count;
            load_glyph_data(glyph_data, font_data, sample_chars[sample_number]);
            outline_view.center_x = glyph_data.center_x;
            outline_view.center_y = glyph_data.center_y;
            is_changed = true;
        }

        // 表示の切り替え
        // View toggle.
        for (int k = 0; k < view_count; k++) {
            if (key_pressed(canvas, view_keys[k], was_down) && k != view) {
                view = k;
                is_changed = true;
            }
        }

        if (key_pressed(canvas, 'P', was_down)) {
            outline_view.show_points = !outline_view.show_points;
            std::printf("points: %s (green: on the curve, red: off the curve, light blue: implied)\n", outline_view.show_points ? "on" : "off");
        }
        if (key_pressed(canvas, 'R', was_down)) {
            outline_view.em_pixels = default_em_pixels;
            outline_view.center_x = glyph_data.center_x;
            outline_view.center_y = glyph_data.center_y;
        }

        // 拡大と移動。押している間、少しずつ動く。1秒で拡大は 4 倍、移動は 90 ピクセル
        // Zoom and pan. Move gradually while held: zoom changes by 4x per second and pan by 90 pixels per second.
        if (view == view_outline && font_data.status == font_ok) {
            if (canvas.is_key_down('Z')) {
                outline_view.em_pixels *= std::pow(4.0f, canvas.delta_time);
            }
            if (canvas.is_key_down('X')) {
                outline_view.em_pixels /= std::pow(4.0f, canvas.delta_time);
            }
            outline_view.em_pixels = std::clamp(outline_view.em_pixels, min_em_pixels, max_em_pixels);

            // 移動の量は、画面のピクセルで決める。見る場所はフォントの単位なので、1 ピクセルが何単位かで割る
            // 絵が矢印の向きに動くように、見る場所は逆向きに動かす。y は、フォントが上向き、画面が下向き
            // Set movement in screen pixels. The view position uses font units, so divide by font units per pixel.
            // Move the view in the opposite direction so the image moves with the arrow. Font y points up; screen y points down.
            const float scale = outline_view.em_pixels / static_cast<float>(font_data.font.units_per_em);
            const float step = 90.0f * canvas.delta_time / scale;
            if (canvas.is_key_down(key_left)) {
                outline_view.center_x += step;
            }
            if (canvas.is_key_down(key_right)) {
                outline_view.center_x -= step;
            }
            if (canvas.is_key_down(key_up)) {
                outline_view.center_y -= step;
            }
            if (canvas.is_key_down(key_down)) {
                outline_view.center_y += step;
            }

            // 文字から遠くへ動かし続けると、画面の外まで長い線を引くことになり、重くなる。見る場所を、文字の近くに収める
            // If the view moves far from the text, it draws long lines offscreen and becomes slow. Keep the view near the glyph.
            const float limit = static_cast<float>(font_data.font.units_per_em) * 2.0f;
            outline_view.center_x = std::clamp(outline_view.center_x, -limit, limit);
            outline_view.center_y = std::clamp(outline_view.center_y, -limit, limit);
        }

        if (is_changed) {
            print_report(view, font_entries[font_number], font_data, glyph_data);
        }

        // 毎フレーム、背景を暗い青で塗り直す
        // Repaint the background dark blue every frame.
        fill_rect(canvas, 0, 0, canvas.width, canvas.height, background);

        const FontEntry& entry = font_entries[font_number];
        if (font_data.status != font_ok) {
            draw_unavailable(canvas, entry, font_data);
        } else if (view == view_outline) {
            draw_outline_view(canvas, entry, font_data, glyph_data, outline_view);
        } else if (view == view_tables) {
            draw_tables_view(canvas, entry, font_data);
        } else if (view == view_members) {
            draw_members_view(canvas, entry, font_data);
        } else {
            draw_bytes_view(canvas, entry, glyph_data);
        }
        draw_indicators(canvas, view, font_number);

        canvas.present();
    }
}
