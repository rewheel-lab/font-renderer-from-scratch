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

// ここに、この回で書くものを足していく
//   draw_glyph_points … グリフの輪郭の点を、画面に点として打つ関数
// フォントを読む関数（load_font、find_glyph_index、load_glyph など）は、font.cpp に書く
// Add the items for this episode here.
// draw_glyph_points ... Plot glyph outline points on screen.
// Write font-reading functions (load_font, find_glyph_index, load_glyph, and others) in font.cpp.

int main() {
    Canvas canvas(320, 180, 4);  // 320×180 の絵を、4倍に拡大して表示する / Display a 320x180 image enlarged 4 times.

    // この回で、ここに、フォントを読む処理を書く
    //   読むフォントのファイルの場所（Windows に入っているもの）と、読んだフォントを入れる Font、'A' と 'あ' の Glyph を用意する
    //   load_font で読み、find_glyph_index でグリフの番号を求め、load_glyph で輪郭の点を読む
    //   ファイルは大きいので、ループに入る前に、1回だけ読む
    // Write the font-reading code here.
    // Set the file path for an installed Windows font, a Font to hold it, and Glyph values for 'A' and 'あ'.
    // Read it with load_font, find each glyph ID with find_glyph_index, and read outline points with load_glyph.
    // Font files are large, so read the file once before the loop.

    int frame = 0;  // 何フレーム目か。最初のフレームが 0 / Current frame number. The first frame is 0.

    while (canvas.update()) {
        // 画面全体を塗る。赤は x に、緑は y に合わせて 0〜255 に変える
        // draw_text は文字の点だけを打つので、毎フレーム背景を塗り直さないと、前の文字が残る
        // この回で、この背景を無地に差し替える（点が見やすいように）
        // Fill the whole screen. Map x to red and y to green from 0 to 255.
        // draw_text plots only the text pixels, so repaint the background every frame or old text remains.
        // In this episode, replace this background with a solid color so points are easy to see.
        for (int y = 0; y < canvas.height; y++) {
            for (int x = 0; x < canvas.width; x++) {
                const uint8_t r = static_cast<uint8_t>(x * 255 / (canvas.width - 1));
                const uint8_t g = static_cast<uint8_t>(y * 255 / (canvas.height - 1));
                set_pixel(canvas, x, y, Color{r, g, 0});
            }
        }

        // この回で、ここの frame と time の表示を、読んだ値（表の数、グリフの番号、点の数など）の表示に差し替える
        // フォントの輪郭の点は、draw_glyph_points で、この下に描く
        // In this episode, replace the frame and time display here with the values read from the font (such as table count, glyph ID, and point count).
        // Draw the font outline points below with draw_glyph_points.

        // フレームの番号と経過時間を、2行の文字列にする
        // snprintf は printf と同じ書式で、画面ではなく文字の入れ物（buffer）に書く。sizeof(buffer) より多くは書かない
        // \n は改行、%d は整数、%.2f は小数点以下2桁の小数
        // Format the frame number and elapsed time as two lines of text.
        // snprintf uses printf-style formatting and writes to a string buffer instead of the screen. It will not write more than sizeof(buffer).
        // \n is a newline, %d is an integer, and %.2f is a number with two decimal places.
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "frame %d\ntime %.2f", frame, canvas.time);

        // 左上に、2倍の大きさで描く。画面のふちから4ピクセル空ける
        // Draw at the top-left at 2x size, with a 4-pixel margin from the screen edge.
        const Color white = {255, 255, 255};
        draw_text(canvas, 4, 4, buffer, white, 2);

        frame++;
        canvas.present();
    }
}
