#include "font.h"

#include <cstdio>
#include <fstream>

// グリフの点ごとにある、1バイトのフラグで、立っているビットの意味
// 点の位置は、前の点からの差で並んでいる。フラグが、その差の読み方を決める
// Meaning of each set bit in the 1-byte flag for a glyph point.
// Point positions are stored as differences from the previous point. The flags determine how to read each difference.
const int flag_on_curve = 0x01;  // 曲線の上の点 / On-curve point.
// x の差が 1バイト（0〜255）。立っていなければ 2バイト（マイナスもある）
// The x difference uses 1 byte (0 to 255). If the flag is off, it uses 2 bytes (signed).
const int flag_x_short = 0x02;  
// y の差が 1バイト。立っていなければ 2バイト
// The y difference uses 1 byte. If the flag is off, it uses 2 bytes.
const int flag_y_short = 0x04;  
// 同じフラグが、次の1バイトの数だけ、続けて繰り返される（フラグの節約）
// Repeat the same flag for the count in the next byte (to save space).
const int flag_repeat = 0x08;  
// x が 1バイトなら「差がプラス」、2バイトなら「前の点と同じ x」（何も読まない）
// If x uses 1 byte, the difference is positive. If it uses 2 bytes, x matches the previous point (read nothing).
const int flag_x_same_or_positive = 0x10;  
const int flag_y_same_or_positive = 0x20;  // y も同じ / The same applies to y.

// ファイルを丸ごと読んで、バイトの列にする。読めたら true
// Read the whole file into a byte array. Return true if reading succeeds.
bool read_file(const char* path, std::vector<uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::printf("cannot open: %s\n", path);
        return false;
    }

    // ファイルの終わりまで進んで大きさを調べ、その大きさのバイトの列を用意してから、先頭に戻って全部読む
    // Seek to the end of the file to get its size, allocate that many bytes, then return to the start and read the whole file.
    file.seekg(0, std::ios::end);
    const int file_size = static_cast<int>(file.tellg());
    file.seekg(0, std::ios::beg);
    bytes.resize(file_size);
    file.read(reinterpret_cast<char*>(bytes.data()), file_size);
    return true;
}

// バイトの列の offset 番目から、2バイトの整数を読む
// フォントファイルの数は、大きい桁のバイトから先に並んでいる（ビッグエンディアン）
// 数を書くときに、千の位から先に書くのと同じ。だから、先に読んだバイトほど大きい桁にずらして足す
// Read a 2-byte integer from a byte array at offset.
// Font files store the most significant byte first (big-endian).
// As when writing a number from the thousands place down, shift earlier bytes to higher places before adding them.
uint16_t read_u16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

// 4バイトの整数を読む。1バイトずつ uint32_t に入れてからずらす（int のままだと、最上位のバイトをずらしたときに溢れる）
// Read a 4-byte integer. Put each byte into uint32_t before shifting; shifting the highest byte while it is still an int can overflow.
uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    const uint32_t b0 = bytes[offset];
    const uint32_t b1 = bytes[offset + 1];
    const uint32_t b2 = bytes[offset + 2];
    const uint32_t b3 = bytes[offset + 3];
    return (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
}

// 2バイトの整数を、マイナスも含めて読む。同じ2バイトを int16_t として見直すと、32768 以上がマイナスになる
// Read a 2-byte integer as signed. Interpreting the same bytes as int16_t makes values from 32768 negative.
int16_t read_i16(const std::vector<uint8_t>& bytes, size_t offset) {
    return static_cast<int16_t>(read_u16(bytes, offset));
}

// offset 番目からの4バイトが、tag（"head" のような4文字）と同じか
// Whether the 4 bytes at offset match the tag (a 4-character name such as "head").
static bool has_tag(const std::vector<uint8_t>& bytes, size_t offset, const char* tag) {
    for (size_t i = 0; i < 4; i++) {
        if (bytes[offset + i] != static_cast<uint8_t>(tag[i])) {
            return false;
        }
    }
    return true;
}

// 表の目次から、tag の表を探して、ファイルの先頭から数えた位置を返す。見つからなければ 0
// 目次の1件は16バイト：タグ（4）、チェックサム（4）、表の位置（4）、表の長さ（4）
// 目次そのものの頭には、12バイトの見出しがある
// Find the table tagged tag in the directory and return its offset from the start of the file. Return 0 if missing.
// Each directory entry is 16 bytes: tag (4), checksum (4), table offset (4), and table length (4).
// The directory starts with a 12-byte header.
static size_t find_table(const Font& font, const char* tag) {
    for (int i = 0; i < font.num_tables; i++) {
        const size_t record = font.directory_offset + 12 + 16 * static_cast<size_t>(i);
        if (!has_tag(font.data, record, tag)) {
            continue;
        }

        // 表の位置と長さがファイルの外を指していたら、壊れたファイルとして扱う
        // Treat the file as damaged if a table offset or length goes outside the file.
        const size_t offset = read_u32(font.data, record + 8);
        const size_t length = read_u32(font.data, record + 12);
        if (offset + length > font.data.size()) {
            std::printf("table %s is outside the file (offset %zu, length %zu, file size %zu)\n", tag, offset, length, font.data.size());
            return 0;
        }
        return offset;
    }

    std::printf("table not found: %s\n", tag);
    return 0;
}

// フォントのファイルを読んで、font に入れる。読めて、この回で扱える形なら true
// Read a font file into font. Return true if it loads and uses a format supported by this episode.
bool load_font(Font& font, const char* path) {
    if (!read_file(path, font.data)) {
        return false;
    }
    const std::vector<uint8_t>& data = font.data;
    std::printf("loaded: %s (%d bytes)\n", path, static_cast<int>(data.size()));

    // 先頭が "ttcf" なら TTC。1つのファイルに複数のフォントが入っていて、先頭に各フォントの目次の位置が並んでいる
    //   0: "ttcf"、4: バージョン、8: フォントの数、12: 1番目のフォントの目次の位置、16: 2番目…
    // ここでは先頭のフォントだけを使う。そうでなければ、目次はファイルの先頭にある
    // If the first tag is "ttcf", this is a TTC. It contains multiple fonts, with each table-directory offset at the start.
    // 0: "ttcf", 4: version, 8: number of fonts, 12: first font directory offset, 16: second, and so on.
    // Use only the first font here. Otherwise, the directory is at the start of the file.
    font.directory_offset = 0;
    if (has_tag(data, 0, "ttcf")) {
        const uint32_t font_count = read_u32(data, 8);
        font.directory_offset = read_u32(data, 12);
        std::printf("TTC: %u fonts, using the first one (its directory is at %zu)\n", font_count, font.directory_offset);
    }
    if (font.directory_offset + 12 > data.size()) {
        std::printf("the table directory is outside the file: %zu\n", font.directory_offset);
        return false;
    }

    // 表の目次の見出し：0: sfntVersion（4）、4: 表の数（2）、6〜11: 探しやすくするための数（使わない）
    // Table-directory header: 0: sfntVersion (4 bytes), 4: table count (2), 6 to 11: search values (unused).
    font.num_tables = read_u16(data, font.directory_offset + 4);
    std::printf("tables: %d\n", font.num_tables);

    const size_t head_offset = find_table(font, "head");
    const size_t maxp_offset = find_table(font, "maxp");
    const size_t cmap_offset = find_table(font, "cmap");
    font.loca_offset = find_table(font, "loca");
    font.glyf_offset = find_table(font, "glyf");
    if (head_offset == 0 || maxp_offset == 0 || cmap_offset == 0 || font.loca_offset == 0 || font.glyf_offset == 0) {
        // CFF の輪郭を持つフォント（OpenType の .otf など）には、glyf も loca もない
        // Fonts with CFF outlines (such as OpenType .otf files) have neither glyf nor loca.
        std::printf("this font has no TrueType outline tables\n");
        return false;
    }

    // head の12バイト目には、マジックナンバー 0x5F0F3CF5 が入っていると決まっている
    // 読み方を間違えていないかの確かめにもなる
    // The 12th byte of head must contain magic number 0x5F0F3CF5.
    // This also checks that the table was read correctly.
    const uint32_t magic = read_u32(data, head_offset + 12);
    std::printf("head magic number: 0x%08X (it must be 0x5F0F3CF5)\n", magic);
    if (magic != 0x5F0F3CF5) {
        return false;
    }

    // head：18: unitsPerEm（2）、50: indexToLocFormat（2）
    font.units_per_em = read_u16(data, head_offset + 18);
    font.index_to_loc_format = read_i16(data, head_offset + 50);

    // maxp：4: numGlyphs（2）
    font.num_glyphs = read_u16(data, maxp_offset + 4);

    // cmap：2: 表の数（2）、4〜: 目次（1件が8バイト：platformID（2）、encodingID（2）、表の位置（4））
    // 表の位置は、cmap の頭から数える。platformID が 3（Windows）で encodingID が 1（Unicode）のものを使う
    // cmap: 2: number of records (2 bytes); 4 onward: records (8 bytes each: platformID (2), encodingID (2), table offset (4)).
    // Offsets are relative to the start of cmap. Use platformID 3 (Windows) and encodingID 1 (Unicode).
    font.cmap_offset = 0;
    const int cmap_count = read_u16(data, cmap_offset + 2);
    for (int i = 0; i < cmap_count; i++) {
        const size_t record = cmap_offset + 4 + 8 * static_cast<size_t>(i);
        const int platform_id = read_u16(data, record);
        const int encoding_id = read_u16(data, record + 2);
        if (platform_id == 3 && encoding_id == 1) {
            font.cmap_offset = cmap_offset + read_u32(data, record + 4);
        }
    }

    // 表の先頭の2バイトが形式（format）。この回は形式 4（0〜0xFFFF の文字コードを、区間でまとめた表）だけ読む
    // The first 2 bytes of a table give its format. This episode reads only format 4 (ranges covering code points 0 to 0xFFFF).
    if (font.cmap_offset == 0 || read_u16(data, font.cmap_offset) != 4) {
        std::printf("this font has no Windows Unicode cmap of format 4\n");
        return false;
    }

    std::printf("units per em: %d, glyphs: %d, loca format: %d\n", font.units_per_em, font.num_glyphs, font.index_to_loc_format);
    return true;
}

// 文字コード（Unicode）から、グリフの番号を求める
// 形式 4 の表は、文字コードを「区間」に分けて持っている。区間の数は先頭に書いてあり、同じ長さの配列が4つ続く
//   終わりの文字コード（end_codes）、始まりの文字コード（start_codes）、足す数（id_deltas）、idRangeOffset（id_range_offsets）
// Find the glyph ID from a Unicode code point.
// Format 4 stores code points in ranges. The range count is at the start, followed by four arrays of equal length.
// End code points (end_codes), start code points (start_codes), deltas (id_deltas), and idRangeOffset values (id_range_offsets).
int find_glyph_index(const Font& font, uint32_t char_code) {
    const std::vector<uint8_t>& data = font.data;

    // 形式 4 は、2バイトに入る文字コードしか持てない
    // Format 4 can store only code points that fit in 2 bytes.
    if (char_code > 0xFFFF) {
        return 0;
    }

    const size_t table = font.cmap_offset;
    // 表の中には、区間の数の2倍が入っている
    // The table stores twice the number of ranges.
    const size_t segment_count = read_u16(data, table + 6) / 2;  
    const size_t end_codes = table + 14;
    // 終わりの配列のあとに、2バイトの詰め物がある
    // Two padding bytes follow the end-code array.
    const size_t start_codes = end_codes + 2 * segment_count + 2;  
    const size_t id_deltas = start_codes + 2 * segment_count;
    const size_t id_range_offsets = id_deltas + 2 * segment_count;

    // 区間は終わりの文字コードの小さい順に並んでいる。char_code 以上の終わりを持つ、最初の区間が候補
    // Ranges are sorted by ending code point. The first range whose end is at least char_code is the candidate.
    for (size_t i = 0; i < segment_count; i++) {
        const uint32_t end_code = read_u16(data, end_codes + 2 * i);
        if (char_code > end_code) {
            continue;
        }

        // 候補の区間の始まりより小さければ、どの区間にも入らない（フォントにない文字）
        // If it is less than the start of the candidate range, it is in no range (the character is not in the font).
        const uint32_t start_code = read_u16(data, start_codes + 2 * i);
        if (char_code < start_code) {
            return 0;
        }

        const uint32_t delta = read_u16(data, id_deltas + 2 * i);
        const size_t range_offset = read_u16(data, id_range_offsets + 2 * i);

        // idRangeOffset が 0 なら、文字コードに delta を足したものがグリフの番号。足し算は 65536 で一周する
        // When idRangeOffset is 0, the glyph ID is the character code plus delta. The addition wraps at 65536.
        if (range_offset == 0) {
            return static_cast<int>((char_code + delta) & 0xFFFF);
        }

        // 0 でなければ、idRangeOffset の位置から range_offset バイト先に、この区間のグリフの番号の配列がある
        // その中の、char_code の分を読む。0 は「ない」の印なので、delta は足さずに 0 のまま返す
        // If it is not 0, an array of glyph IDs for this range starts range_offset bytes after idRangeOffset.
        // Read the entry for char_code. 0 means "missing," so return 0 without adding delta.
        const size_t position = id_range_offsets + 2 * i + range_offset + 2 * (char_code - start_code);
        const uint32_t glyph_index = read_u16(data, position);
        if (glyph_index == 0) {
            return 0;
        }
        return static_cast<int>((glyph_index + delta) & 0xFFFF);
    }

    return 0;
}

// グリフの番号から、輪郭の点を読む
// Read outline points by glyph ID.
Glyph load_glyph(const Font& font, int glyph_index) {
    Glyph glyph;
    const std::vector<uint8_t>& data = font.data;

    if (glyph_index < 0 || glyph_index >= font.num_glyphs) {
        return glyph;
    }

    // loca には、グリフの番号ごとに「glyf の先頭から数えた、そのグリフの始まる位置」が並んでいる（グリフの数 + 1 個）
    // 次のグリフの始まりが、このグリフの終わり
    // loca stores each glyph's start offset from the beginning of glyf (one more entry than the number of glyphs).
    // The next glyph's start is this glyph's end.
    const size_t i = static_cast<size_t>(glyph_index);
    size_t begin = 0;
    size_t end = 0;
    if (font.index_to_loc_format == 0) {
        // 短い形：2バイトで、実際の位置の半分が入っている（グリフは必ず偶数の位置から始まるため）
        // Short format: 2 bytes storing half the actual offset (glyphs always start at even positions).
        begin = static_cast<size_t>(read_u16(data, font.loca_offset + 2 * i)) * 2;
        end = static_cast<size_t>(read_u16(data, font.loca_offset + 2 * (i + 1))) * 2;
    } else {
        // 長い形：4バイトで、実際の位置が入っている
        // Long format: 4 bytes containing the actual position.
        begin = read_u32(data, font.loca_offset + 4 * i);
        end = read_u32(data, font.loca_offset + 4 * (i + 1));
    }

    // 始まりと終わりが同じなら、データがない。空白などがそう
    // If start and end are the same, there is no data, as with a space.
    if (begin == end) {
        return glyph;
    }

    // グリフの頭：0: 輪郭の数（2。マイナスなら複合グリフ）、2〜9: 外側の四角（xMin、yMin、xMax、yMax。使わない）
    // Glyph header: 0: number of contours (2; negative means a composite glyph). 2 to 9: bounding box (xMin, yMin, xMax, yMax; unused).
    const size_t start = font.glyf_offset + begin;
    const int contour_count = read_i16(data, start);
    if (contour_count < 0) {
        // 複合グリフは、ほかのグリフを、ずらしたり拡大したりして組み合わせたもの。この回では読まない
        // A composite glyph combines other glyphs with offsets or scaling. Do not read them in this episode.
        std::printf("glyph %d is a composite glyph (not supported)\n", glyph_index);
        return glyph;
    }
    if (contour_count == 0) {
        return glyph;
    }

    // 輪郭ごとの「最後の点の番号」が並ぶ。最後の輪郭の最後の点の番号 + 1 が、点の数
    // List of last-point indices for each contour. The last contour's last index plus 1 is the point count.
    size_t p = start + 10;
    for (int c = 0; c < contour_count; c++) {
        glyph.contour_ends.push_back(read_u16(data, p));
        p += 2;
    }
    const int point_count = glyph.contour_ends.back() + 1;

    // そのあとに、ヒンティングの命令（小さい文字を整えるプログラム）がある。まだ使わないので、長さだけ読んで飛ばす
    // Hinting instructions (a program that adjusts small glyphs) follow. They are not used yet, so read their length and skip them.
    const int instruction_length = read_u16(data, p);
    p += 2 + static_cast<size_t>(instruction_length);

    // フラグが、点の数だけ並ぶ（1点につき1バイト）
    // flag_repeat が立っていたら、次の1バイトの回数だけ、同じフラグがさらに続く
    // One flag byte per point.
    // If flag_repeat is set, the next byte gives how many more times the same flag repeats.
    std::vector<uint8_t> flags;
    while (static_cast<int>(flags.size()) < point_count) {
        const uint8_t flag = data[p];
        p++;
        flags.push_back(flag);
        if (flag & flag_repeat) {
            const int repeat_count = data[p];
            p++;
            for (int r = 0; r < repeat_count; r++) {
                flags.push_back(flag);
            }
        }
    }

    // x が、全部の点の分まとめて並ぶ。そのあとに y が、全部の点の分並ぶ
    // どちらも、前の点からの差。最初の点は、原点 (0, 0) からの差
    // All x values are stored together, followed by all y values.
    // Each value is a difference from the previous point. The first point is relative to the origin (0, 0).
    glyph.points.resize(point_count);
    int x = 0;
    for (int k = 0; k < point_count; k++) {
        const int flag = flags[k];
        if (flag & flag_x_short) {
            // 1バイトで、大きさだけが入っている。向きは flag_x_same_or_positive で決まる
            // One byte stores only the magnitude. flag_x_same_or_positive determines the direction.
            const int dx = data[p];
            p++;
            x += (flag & flag_x_same_or_positive) ? dx : -dx;
        } else if (!(flag & flag_x_same_or_positive)) {
            // 2バイトの、マイナスもある数
            // A signed 2-byte number.
            x += read_i16(data, p);
            p += 2;
        }
        // 1バイトでもなく、2バイトでもないとき（flag_x_same_or_positive だけが立っている）は、前の点と同じ x。何も読まない
        // When there is neither one nor two bytes (only flag_x_same_or_positive is set), x matches the previous point. Read nothing.
        glyph.points[k].x = x;
        glyph.points[k].on_curve = (flag & flag_on_curve) != 0;
    }

    int y = 0;
    for (int k = 0; k < point_count; k++) {
        const int flag = flags[k];
        if (flag & flag_y_short) {
            const int dy = data[p];
            p++;
            y += (flag & flag_y_same_or_positive) ? dy : -dy;
        } else if (!(flag & flag_y_same_or_positive)) {
            y += read_i16(data, p);
            p += 2;
        }
        glyph.points[k].y = y;
    }

    return glyph;
}
