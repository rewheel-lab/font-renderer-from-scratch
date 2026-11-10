#include <cstdint>
#include <cstdio>

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

int main() {
    Canvas canvas(320, 180, 4);  // 320×180 の絵を、4倍に拡大して表示する / Display a 320x180 image enlarged 4 times.

    int frame = 0;  // 何フレーム目か。最初のフレームが 0 / Current frame number. The first frame is 0.

    while (canvas.update()) {
        // 画面全体を塗る。赤は x に、緑は y に合わせて 0〜255 に変える
        // draw_text は文字の点だけを打つので、毎フレーム背景を塗り直さないと、前の文字が残る
        // Fill the whole screen. Map x to red and y to green from 0 to 255.
        // draw_text plots only the text pixels, so repaint the background every frame or old text remains.
        for (int y = 0; y < canvas.height; y++) {
            for (int x = 0; x < canvas.width; x++) {
                const uint8_t r = static_cast<uint8_t>(x * 255 / (canvas.width - 1));
                const uint8_t g = static_cast<uint8_t>(y * 255 / (canvas.height - 1));
                set_pixel(canvas, x, y, Color{r, g, 0});
            }
        }

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
