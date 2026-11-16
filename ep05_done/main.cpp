#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numbers>
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

// 辺を1本。輪郭を細かい線分に分けたときの、線分1本分。start から end へ向かう
// One edge: one segment when an outline is divided into short line segments, running from start to end.
struct Edge {
    Vec2 start;
    Vec2 end;
};

// 行と辺が交わる点を1つ
// One point where a row crosses an edge.
struct Crossing {
    float x;        // 交わる位置 / Crossing position.
    int direction;  // 辺が画面の上へ向かっていれば +1、下へ向かっていれば -1 / +1 if the edge goes up on screen, -1 if it goes down.
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

// 2次ベジェ曲線を、細かい線分に分けて、辺として足す
// p0 から p2 まで進む。p1 は曲線を引っ張る点で、曲線は p1 までは届かない
// Split the quadratic Bezier curve into short line segments and add them as edges.
// Move from p0 to p2. p1 pulls the curve, but the curve does not reach p1.
void add_quadratic_bezier_edges(std::vector<Edge>& edges, Vec2 p0, Vec2 p1, Vec2 p2) {
    // 分ける数は、曲線の長さに合わせる。長い曲線を少ない線分で分けると角ばるし、短い曲線を細かく分けても無駄になるため
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
        edges.push_back(Edge{previous, current});
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

// 輪郭を1つ、辺の並びにして足す。points は make_outline で並べ直した、1つの輪郭の点
// 辺は、輪郭をたどる向きのままに足す。向きが、あとで内側か外側かを決める手がかりになる
// Add one contour as a sequence of edges. points contains the reordered points for one contour from make_outline.
// Add the edges in the direction that traces the contour. That direction later helps determine which side is inside.
void add_contour_edges(std::vector<Edge>& edges, const std::vector<OutlinePoint>& points) {
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
            // 次も上の点なら、まっすぐな辺
            // If the next point is also on-curve, add a straight edge.
            edges.push_back(Edge{current, next.position});
            current = next.position;
            i++;
        } else {
            // 次が外の点なら、その次は必ず上の点。外の点に引っ張られる曲線で、そこまで進む
            // If the next point is off-curve, the point after it is on-curve. Follow the curve pulled by the off-curve point to that point.
            const OutlinePoint& end = points[(start + i + 1) % total];
            add_quadratic_bezier_edges(edges, current, next.position, end.position);
            current = end.position;
            i += 2;
        }
    }
}

// グリフの輪郭すべてを、辺の並びにする
// 「あ」のように、輪郭が複数あることもある。塗るときは、すべての輪郭の辺をひとまとめにして調べる
// Turn all glyph contours into edge lists.
// A glyph such as あ can have multiple contours. When filling, check the edges from all contours together.
std::vector<Edge> make_edges(const Glyph& glyph, float origin_x, float baseline_y, float scale) {
    const std::vector<std::vector<OutlinePoint>> contours = make_outline(glyph, origin_x, baseline_y, scale);

    std::vector<Edge> edges;
    for (int c = 0; c < static_cast<int>(contours.size()); c++) {
        add_contour_edges(edges, contours[c]);
    }
    return edges;
}

// 辺を、線で描く
// 塗った文字の上に重ねて、塗りが輪郭からずれていないか確かめるときに使う
// Draw the edges as lines.
// Use this over a filled glyph to check whether the fill is aligned with the outline.
void draw_edges(Canvas& canvas, const std::vector<Edge>& edges, Color color) {
    for (int i = 0; i < static_cast<int>(edges.size()); i++) {
        draw_line(canvas, edges[i].start, edges[i].end, color);
    }
}

// 交点を、x の小さい順（左から右）に並べるための比べ方。std::sort に渡す
// Comparison for sorting crossings by x from left to right. Pass it to std::sort.
bool compare_crossing_x(const Crossing& a, const Crossing& b) {
    return a.x < b.x;
}

// 辺で囲まれた中を、color で塗る
// 1行ずつ、ピクセルの中心の高さ（y + 0.5）で辺と交わる位置を集めて、左から順に調べる
// Fill the area enclosed by the edges with color.
// Process one row at a time. Collect edge intersections at the pixel-center height (y + 0.5) and scan them from left to right.
void fill_edges(Canvas& canvas, const std::vector<Edge>& edges, Color color) {
    std::vector<Crossing> crossings;

    for (int y = 0; y < canvas.height; y++) {
        // 行の真ん中の高さ。ピクセルの角（整数の高さ）で調べると、整数の位置にある頂点や辺が、いつも判定にぴったり乗ってしまう。真ん中なら、ぴったり乗ることが減る
        // Height at the middle of a row. Checking at pixel corners (integer heights) often lands exactly on vertices and edges. Checking at the center avoids this.
        const float center_y = static_cast<float>(y) + 0.5f;

        // この行と交わる辺を探して、交わる位置と向きを集める
        // Find edges that cross this row and collect their crossing positions and directions.
        crossings.clear();
        for (int i = 0; i < static_cast<int>(edges.size()); i++) {
            const Edge& edge = edges[i];

            // 水平な辺は、行と重なるか、まったく交わらないかのどちらか。交わる位置が決まらないので、飛ばす。下の割り算もできない
            // A horizontal edge either overlaps the row or does not cross it. Skip it because its crossing position is undefined; the division below would also fail.
            if (edge.start.y == edge.end.y) {
                continue;
            }

            // 辺の上端と下端。画面の y は下向きなので、y が小さいほうが画面の上
            // The top and bottom of an edge. Screen y increases downward, so the smaller y is higher on screen.
            const float top = std::min(edge.start.y, edge.end.y);
            const float bottom = std::max(edge.start.y, edge.end.y);

            // 上端は含めて、下端は含めない（左上ルールと同じ向き）。行の高さがちょうど頂点（2本の辺のつなぎ目）に来たとき、両方の辺が数えると、同じ位置を2回数えてしまう
            // 上の辺の下端は数えずに、下の辺の上端だけ数えれば、通り抜ける頂点は1回だけ数えられる（塗りのルールで、境目のピクセルを2回塗らないのと同じ）
            // 山や谷の頂点は、2本とも数えるか、2本とも数えないか。向きが逆なので、+1 と -1 で打ち消し合う
            // Include the top edge and exclude the bottom edge (same direction as the top-left rule). If a row meets a vertex (join of two edges), counting both edges would count the same position twice.
            // Exclude the lower endpoint of the upper edge and include only the upper endpoint of the lower edge, so a through-vertex is counted once. This is like avoiding double-filling a boundary pixel.
            // At a peak or valley, either count both edges or neither. Their directions are opposite, so +1 and -1 cancel.
            if (center_y < top || center_y >= bottom) {
                continue;
            }

            // 辺の上で、行の高さにあたる位置。start から end までの割合 t を求めて、x を出す
            // Find the position on the edge at the row height. Find the fraction t from start to end and use it to calculate x.
            const float t = (center_y - edge.start.y) / (edge.end.y - edge.start.y);
            const float x = edge.start.x + (edge.end.x - edge.start.x) * t;

            // 画面の上へ向かう辺は +1、下へ向かう辺は -1
            // An edge going up on screen is +1; one going down is -1.
            const int direction = (edge.end.y < edge.start.y) ? 1 : -1;
            crossings.push_back(Crossing{x, direction});
        }

        // 交わる辺がない行は、塗るものがない
        // A row with no crossings has nothing to fill.
        if (crossings.empty()) {
            continue;
        }
        std::sort(crossings.begin(), crossings.end(), compare_crossing_x);

        // 左のピクセルから順に、中心より左にある交点の向きを足していく
        // 合計（巻き数）は、そのピクセルの中心のまわりを、輪郭が何回まわっているか。0 でなければ内側なので塗る（ノンゼロ）
        // 外側の輪郭と穴の輪郭は、まわる向きが逆。穴の中は +1 と -1 で 0 に戻るので、塗られずに抜ける
        // 1つおきに塗る方法（偶奇）だと、輪郭が重なったところが抜けてしまう。TrueType は、0 でないところを塗ると決まっている
        // For each pixel from left to right, add the directions of crossings to the left of its center.
        // The total (winding count) is how many times the outline winds around the pixel center. Fill if it is not 0 (nonzero rule).
        // Outer contours and hole contours wind in opposite directions. Inside a hole, +1 and -1 cancel to 0, leaving it empty.
        // The alternating fill rule would leave overlaps between contours empty. TrueType uses the nonzero rule.
        int winding = 0;
        int next = 0;
        for (int x = 0; x < canvas.width; x++) {
            const float center_x = static_cast<float>(x) + 0.5f;

            // 交点が中心とちょうど同じ位置なら、左にあるものとして数える。左のふちは塗り、右のふちは塗らない
            // If a crossing is exactly at the center, count it as being on the left. Fill the left edge and leave the right edge empty.
            while (next < static_cast<int>(crossings.size()) && crossings[next].x <= center_x) {
                winding += crossings[next].direction;
                next++;
            }

            if (winding != 0) {
                set_pixel(canvas, x, y, color);
            }
        }
    }
}

// グリフの中を塗る
// Fill the inside of a glyph.
void fill_glyph(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale, Color color) {
    const std::vector<Edge> edges = make_edges(glyph, origin_x, baseline_y, scale);
    fill_edges(canvas, edges, color);
}

// グリフの輪郭を、線で描く
// Draw glyph outlines as lines.
void draw_glyph_outline(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale, Color color) {
    const std::vector<Edge> edges = make_edges(glyph, origin_x, baseline_y, scale);
    draw_edges(canvas, edges, color);
}

// グリフの輪郭の点を、画面に点として打つ
// 曲線の上の点は緑、外の点は赤、補った上の点は水色にする。輪郭が正しく描けているか、点の種類から確かめるため
// Plot glyph outline points on screen.
// Use green for on-curve points, red for off-curve points, and cyan for added on-curve points. Point types help check whether the outline is drawn correctly.
void draw_glyph_points(Canvas& canvas, const Glyph& glyph, float origin_x, float baseline_y, float scale) {
    const Color green = {64, 255, 64};
    const Color red = {255, 64, 64};
    const Color light_blue = {64, 200, 255};

    // 輪郭を作るときと同じ、補った点も入った並びを使う
    // Use the same point sequence as when building the outline, including the inserted points.
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

// 円を、64角形の辺として足す。clockwise が true なら、画面の上で時計回りにたどる
// 画面の y は下向きなので、角度を増やしながら (cos, sin) の位置をたどると、時計回りになる
// Add a circle as a 64-sided polygon. If clockwise is true, trace it clockwise on screen.
// Screen y points down, so increasing the angle while following (cos, sin) moves clockwise.
void add_circle_edges(std::vector<Edge>& edges, Vec2 center, float radius, bool clockwise) {
    const int corners = 64;
    const float sign = clockwise ? 1.0f : -1.0f;

    for (int i = 0; i < corners; i++) {
        // 隣り合う辺が同じ頂点を共有するよう、頂点は同じ式で出す。最後の頂点は、1周して最初の頂点に戻る
        // Calculate vertices with the same formula so neighboring edges share a vertex. The final vertex wraps around to the first.
        const float angle_a = sign * 2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(corners);
        const float angle_b = sign * 2.0f * std::numbers::pi_v<float> * static_cast<float>((i + 1) % corners) / static_cast<float>(corners);
        const Vec2 a = Vec2{center.x + radius * std::cos(angle_a), center.y + radius * std::sin(angle_a)};
        const Vec2 b = Vec2{center.x + radius * std::cos(angle_b), center.y + radius * std::sin(angle_b)};
        edges.push_back(Edge{a, b});
    }
}

// 輪（ドーナツの形）を、辺として足す
// 外側の円は時計回り、内側の円は反時計回りにたどる。フォントの文字と同じで、穴の輪郭は外側と逆向き
// Add a ring (donut shape) as edges.
// Trace the outer circle clockwise and the inner circle counterclockwise. As with font glyphs, the contour around a hole runs in the opposite direction from the outer contour.
void add_ring_edges(std::vector<Edge>& edges, Vec2 center, float outer_radius, float inner_radius) {
    add_circle_edges(edges, center, outer_radius, true);
    add_circle_edges(edges, center, inner_radius, false);
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

    // true にすると、塗った文字の上に、輪郭の線を重ねて出す。塗りが輪郭からずれていないか確かめるため
    // If true, overlay the outline on the filled glyph to check that the fill matches it.
    const bool show_outline = false;

    // true にすると、輪郭の点も重ねて出す。緑は曲線の上の点、赤は曲線の外の点、水色は補った上の点
    // If true, also show the outline points. Green points are on the curve, red points are off the curve, and cyan points are added points on the curve.
    const bool show_points = false;

    // true にすると、文字の代わりに、重なった2つの輪を塗る。輪郭が重なったところがどう塗られるかを確かめるための図形
    // If true, fill two overlapping rings instead of text. This shape shows how overlapping outlines are filled.
    const bool show_rings = false;

    // ループに入る前に、1回だけ読む。毎フレーム読むと、十数MBのファイルを何度も読み直すことになる
    // Read once before the loop. Reading it every frame would repeatedly load a file over 10 MB.
    Font font;
    Glyph glyph_s;
    Glyph glyph_o;
    Glyph glyph_hiragana;
    char info[256];
    const bool is_loaded = load_font(font, font_path);
    if (is_loaded) {
        // 文字コードから、グリフの番号を求めて、輪郭の点を読む
        // 'O' は、穴のある文字。外側の輪郭と、内側の輪郭の2つでできている
        // Find the glyph ID from the character code and read its outline points.
        // 'O' has a hole and consists of an outer and an inner contour.
        const int index_s = find_glyph_index(font, 'S');
        const int index_o = find_glyph_index(font, 'O');
        const int index_hiragana = find_glyph_index(font, U'あ');
        glyph_s = load_glyph(font, index_s);
        glyph_o = load_glyph(font, index_o);
        glyph_hiragana = load_glyph(font, index_hiragana);

        std::printf("'S': glyph %d, %d points, %d contours\n", index_s, static_cast<int>(glyph_s.points.size()), static_cast<int>(glyph_s.contour_ends.size()));
        std::printf("'O': glyph %d, %d points, %d contours\n", index_o, static_cast<int>(glyph_o.points.size()), static_cast<int>(glyph_o.contour_ends.size()));
        std::printf("U+3042: glyph %d, %d points, %d contours\n", index_hiragana, static_cast<int>(glyph_hiragana.points.size()), static_cast<int>(glyph_hiragana.contour_ends.size()));

        // 読んだ値を、画面にも出す。日本語の文字は draw_text では出せないので、文字コードで書く
        // Also show the read values on screen. draw_text cannot display Japanese, so write the code point.
        std::snprintf(info, sizeof(info), "S      glyph %d, %d points\nO      glyph %d, %d points\nU+3042 glyph %d, %d points",
            index_s, static_cast<int>(glyph_s.points.size()),
            index_o, static_cast<int>(glyph_o.points.size()),
            index_hiragana, static_cast<int>(glyph_hiragana.points.size()));
    } else {
        std::snprintf(info, sizeof(info), "cannot read the font file\n%s", font_path);
    }

    while (canvas.update()) {
        // 毎フレーム、背景を暗い青で塗り直す。文字が見やすいように、無地にする
        // Repaint the background solid dark blue every frame so the text is easy to see.
        const Color background = {16, 16, 48};
        for (int y = 0; y < canvas.height; y++) {
            for (int x = 0; x < canvas.width; x++) {
                set_pixel(canvas, x, y, background);
            }
        }

        const Color white = {255, 255, 255};
        draw_text(canvas, 4, 4, info, white, 1);

        // 輪郭を重ねる線の色。白い塗りの上でも見えるように、オレンジにする
        // Color of the line drawn over the outline. Use orange so it remains visible on the white fill.
        const Color orange = {255, 144, 32};

        if (show_rings) {
            // 重なった2つの輪。それぞれが、外側の円と内側の円でできている
            // Two overlapping rings, each made from an outer and an inner circle.
            std::vector<Edge> ring_edges;
            add_ring_edges(ring_edges, Vec2{125.0f, 100.0f}, 56.0f, 34.0f);
            add_ring_edges(ring_edges, Vec2{195.0f, 100.0f}, 56.0f, 34.0f);
            fill_edges(canvas, ring_edges, white);
            if (show_outline) {
                draw_edges(canvas, ring_edges, orange);
            }
        } else if (is_loaded) {
            // 文字の大きさ（1 em）を、128 ピクセルにする。フォントの単位 1 が何ピクセルになるかは、1 em の単位数で割って求める
            // 3つ並べても画面に収まる大きさにした
            // Set text size (1 em) to 128 pixels. Divide by the font units per em to find pixels per font unit.
            // This is small enough for three glyphs to fit on screen.
            const float em_pixels = 128.0f;
            const float scale = em_pixels / static_cast<float>(font.units_per_em);

            // 文字を並べる基準の線（ベースライン）の高さ。文字はこの線の上に乗る
            // Height of the baseline used to position glyphs. Glyphs sit on this line.
            const float baseline_y = 165.0f;

            // 文字を置く左端の位置（フォントの座標 x = 0 を置く画面の位置）
            // Left position for text (screen position of font coordinate x = 0).
            const float origin_s = 10.0f;
            const float origin_o = 100.0f;
            const float origin_hiragana = 198.0f;

            fill_glyph(canvas, glyph_s, origin_s, baseline_y, scale, white);
            fill_glyph(canvas, glyph_o, origin_o, baseline_y, scale, white);
            fill_glyph(canvas, glyph_hiragana, origin_hiragana, baseline_y, scale, white);

            // 輪郭と点は、塗りの上に重ねる
            // Draw the contours and points over the fill.
            if (show_outline) {
                draw_glyph_outline(canvas, glyph_s, origin_s, baseline_y, scale, orange);
                draw_glyph_outline(canvas, glyph_o, origin_o, baseline_y, scale, orange);
                draw_glyph_outline(canvas, glyph_hiragana, origin_hiragana, baseline_y, scale, orange);
            }
            if (show_points) {
                draw_glyph_points(canvas, glyph_s, origin_s, baseline_y, scale);
                draw_glyph_points(canvas, glyph_o, origin_o, baseline_y, scale);
                draw_glyph_points(canvas, glyph_hiragana, origin_hiragana, baseline_y, scale);
            }
        }

        canvas.present();
    }
}
