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

    // この回で、ここに、何フレーム目かを数える変数を用意する
    // Add a variable here to count the current frame.

    while (canvas.update()) {
        // 画面全体を塗る。赤は x に、緑は y に合わせて 0〜255 に変える
        // Fill the screen. Set red from x and green from y, each from 0 to 255.
        for (int y = 0; y < canvas.height; y++) {
            for (int x = 0; x < canvas.width; x++) {
                const uint8_t r = static_cast<uint8_t>(x * 255 / (canvas.width - 1));
                const uint8_t g = static_cast<uint8_t>(y * 255 / (canvas.height - 1));
                set_pixel(canvas, x, y, Color{r, g, 0});
            }
        }

        // この回で、この白い縦線を消して、かわりに左上にフレームの番号と経過時間を出す
        // 文字は text.cpp の draw_text で描く（draw_text は、この回でこれから作る）
        // Remove this white vertical line in this episode. Show the frame number and elapsed time at the top-left instead.
        // Draw the text with draw_text in text.cpp (created in this episode).

        // x = 100 に白い縦線を引く。y は 0 〜 height - 1 なので、< で止める
        // Draw a white vertical line at x = 100. Since y runs from 0 to height - 1, stop with <.
        const Color white = {255, 255, 255};
        for (int y = 0; y < canvas.height; y++) {
            set_pixel(canvas, 100, y, white);
        }

        canvas.present();
    }
}
