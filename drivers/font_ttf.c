#include <limine.h>
#include <xbks/font.h>
#include <xbks/log.h>
#include <xbks/string.h>

enum {
    TAG_CMAP = 0x636d6170u,
    TAG_GLYF = 0x676c7966u,
    TAG_HEAD = 0x68656164u,
    TAG_HHEA = 0x68686561u,
    TAG_HMTX = 0x686d7478u,
    TAG_LOCA = 0x6c6f6361u,
    TAG_MAXP = 0x6d617870u,
    TAG_OS_2 = 0x4f532f32u,

    ARG_1_AND_2_ARE_WORDS = 1u << 0,
    ARGS_ARE_XY_VALUES = 1u << 1,
    WE_HAVE_A_SCALE = 1u << 3,
    MORE_COMPONENTS = 1u << 5,
    WE_HAVE_AN_X_AND_Y_SCALE = 1u << 6,
    WE_HAVE_A_TWO_BY_TWO = 1u << 7,
    WE_HAVE_INSTRUCTIONS = 1u << 8,

    GLYPH_ON_CURVE = 1u << 0,
    GLYPH_X_SHORT_VECTOR = 1u << 1,
    GLYPH_Y_SHORT_VECTOR = 1u << 2,
    GLYPH_REPEAT_FLAG = 1u << 3,
    GLYPH_X_IS_SAME_OR_POSITIVE = 1u << 4,
    GLYPH_Y_IS_SAME_OR_POSITIVE = 1u << 5,

    MAX_GLYPH_POINTS = 384,
    MAX_GLYPH_CONTOURS = 64,
    MAX_RASTER_EDGES = 768,
    MAX_COMPONENT_DEPTH = 8,
};

struct ttf_table {
    const uint8_t *data;
    size_t size;
};

struct ttf_font {
    const uint8_t *data;
    size_t size;
    struct ttf_table cmap;
    struct ttf_table glyf;
    struct ttf_table head;
    struct ttf_table hhea;
    struct ttf_table hmtx;
    struct ttf_table loca;
    struct ttf_table maxp;
    struct ttf_table os_2;
    const uint8_t *cmap_subtable;
    size_t cmap_subtable_size;
    uint16_t num_glyphs;
    uint16_t number_of_hmetrics;
    uint16_t units_per_em;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    int16_t typo_ascent;
    int16_t typo_descent;
    int16_t typo_line_gap;
    int16_t index_to_loc_format;
};

struct glyph_point {
    int32_t x;
    int32_t y;
    bool on_curve;
};

struct glyph_outline {
    struct glyph_point points[MAX_GLYPH_POINTS];
    uint16_t contours[MAX_GLYPH_CONTOURS];
    size_t point_count;
    size_t contour_count;
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    int16_t advance_width;
    bool has_bounds;
};

struct font_metrics {
    int32_t scale;
    int32_t design_top;
    int32_t design_span;
    int32_t origin_y;
};

struct raster_edge {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;
};

static uint8_t system_font_alpha[XBKS_TTY_FONT_GLYPHS * XBKS_TTY_FONT_WIDTH * XBKS_TTY_FONT_HEIGHT];

static bool range_is_valid(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | (uint16_t)data[1]);
}

static int16_t read_s16(const uint8_t *data) {
    return (int16_t)read_u16(data);
}

static uint32_t read_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
        ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) |
        (uint32_t)data[3];
}

static uint32_t tag_from_string(const uint8_t *tag) {
    return ((uint32_t)tag[0] << 24) |
        ((uint32_t)tag[1] << 16) |
        ((uint32_t)tag[2] << 8) |
        (uint32_t)tag[3];
}

static bool string_ends_with(const char *text, const char *suffix) {
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);

    if (suffix_length > text_length) {
        return false;
    }

    return strcmp(text + text_length - suffix_length, suffix) == 0;
}

static const struct limine_file *find_font_module(const struct limine_module_response *modules) {
    if (modules == 0 || modules->modules == 0) {
        return 0;
    }

    for (uint64_t i = 0; i < modules->module_count; ++i) {
        const struct limine_file *module = modules->modules[i];
        if (module == 0 || module->address == 0 || module->size == 0) {
            continue;
        }

        const char *module_string = module->string != 0 ? module->string : "";
        const char *module_path = module->path != 0 ? module->path : "";

        if (strcmp(module_string, "system-font") == 0 ||
            string_ends_with(module_path, "/system.ttf") ||
            string_ends_with(module_path, "system.ttf")) {
            return module;
        }
    }

    return 0;
}

static bool get_table(const struct ttf_font *font, uint32_t tag, struct ttf_table *out) {
    if (!range_is_valid(font->size, 0, 12)) {
        return false;
    }

    const uint16_t table_count = read_u16(font->data + 4);
    if (!range_is_valid(font->size, 12, (size_t)table_count * 16u)) {
        return false;
    }

    for (uint16_t i = 0; i < table_count; ++i) {
        const uint8_t *record = font->data + 12u + (size_t)i * 16u;
        const uint32_t record_tag = tag_from_string(record);
        const uint32_t offset = read_u32(record + 8);
        const uint32_t length = read_u32(record + 12);

        if (record_tag == tag) {
            if (!range_is_valid(font->size, offset, length)) {
                return false;
            }

            out->data = font->data + offset;
            out->size = length;
            return true;
        }
    }

    return false;
}

static bool select_cmap_subtable(struct ttf_font *font) {
    if (!range_is_valid(font->cmap.size, 0, 4)) {
        return false;
    }

    const uint16_t subtable_count = read_u16(font->cmap.data + 2);
    if (!range_is_valid(font->cmap.size, 4, (size_t)subtable_count * 8u)) {
        return false;
    }

    const uint8_t *fallback = 0;
    size_t fallback_size = 0;

    for (uint16_t i = 0; i < subtable_count; ++i) {
        const uint8_t *record = font->cmap.data + 4u + (size_t)i * 8u;
        const uint16_t platform = read_u16(record);
        const uint16_t encoding = read_u16(record + 2);
        const uint32_t offset = read_u32(record + 4);

        if (!range_is_valid(font->cmap.size, offset, 2)) {
            continue;
        }

        const uint8_t *subtable = font->cmap.data + offset;
        const uint16_t format = read_u16(subtable);
        size_t length = 0;

        if (format == 4) {
            if (!range_is_valid(font->cmap.size, offset, 4)) {
                continue;
            }
            length = read_u16(subtable + 2);
        } else if (format == 12) {
            if (!range_is_valid(font->cmap.size, offset, 16)) {
                continue;
            }
            length = read_u32(subtable + 4);
        } else {
            continue;
        }

        if (!range_is_valid(font->cmap.size, offset, length)) {
            continue;
        }

        if (fallback == 0) {
            fallback = subtable;
            fallback_size = length;
        }

        if ((platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0) {
            font->cmap_subtable = subtable;
            font->cmap_subtable_size = length;
            return true;
        }
    }

    font->cmap_subtable = fallback;
    font->cmap_subtable_size = fallback_size;
    return fallback != 0;
}

static bool parse_ttf(const uint8_t *data, size_t size, struct ttf_font *font) {
    memset(font, 0, sizeof(*font));
    font->data = data;
    font->size = size;

    if (!range_is_valid(size, 0, 12)) {
        return false;
    }

    const uint32_t scaler_type = read_u32(data);
    if (scaler_type != 0x00010000u && scaler_type != 0x74727565u) {
        return false;
    }

    if (!get_table(font, TAG_CMAP, &font->cmap) ||
        !get_table(font, TAG_GLYF, &font->glyf) ||
        !get_table(font, TAG_HEAD, &font->head) ||
        !get_table(font, TAG_HHEA, &font->hhea) ||
        !get_table(font, TAG_HMTX, &font->hmtx) ||
        !get_table(font, TAG_LOCA, &font->loca) ||
        !get_table(font, TAG_MAXP, &font->maxp)) {
        return false;
    }

    (void)get_table(font, TAG_OS_2, &font->os_2);

    if (font->head.size < 54 || font->hhea.size < 36 || font->maxp.size < 6) {
        return false;
    }

    font->units_per_em = read_u16(font->head.data + 18);
    font->index_to_loc_format = read_s16(font->head.data + 50);
    font->ascent = read_s16(font->hhea.data + 4);
    font->descent = read_s16(font->hhea.data + 6);
    font->line_gap = read_s16(font->hhea.data + 8);
    font->number_of_hmetrics = read_u16(font->hhea.data + 34);
    font->num_glyphs = read_u16(font->maxp.data + 4);

    if (font->os_2.size >= 74) {
        font->typo_ascent = read_s16(font->os_2.data + 68);
        font->typo_descent = read_s16(font->os_2.data + 70);
        font->typo_line_gap = read_s16(font->os_2.data + 72);
    }

    if (font->units_per_em == 0 ||
        font->num_glyphs == 0 ||
        font->number_of_hmetrics == 0 ||
        font->number_of_hmetrics > font->num_glyphs ||
        (font->index_to_loc_format != 0 && font->index_to_loc_format != 1)) {
        return false;
    }

    const size_t loca_entry_size = font->index_to_loc_format == 0 ? 2u : 4u;
    if (font->loca.size < ((size_t)font->num_glyphs + 1u) * loca_entry_size ||
        font->hmtx.size < (size_t)font->number_of_hmetrics * 4u) {
        return false;
    }

    return select_cmap_subtable(font);
}

static uint16_t cmap_format4_glyph(const struct ttf_font *font, uint32_t codepoint) {
    const uint8_t *subtable = font->cmap_subtable;
    const size_t size = font->cmap_subtable_size;

    if (codepoint > 0xffffu || size < 16) {
        return 0;
    }

    const uint16_t seg_count = read_u16(subtable + 6) / 2u;
    const size_t end_codes = 14;
    const size_t start_codes = end_codes + (size_t)seg_count * 2u + 2u;
    const size_t id_deltas = start_codes + (size_t)seg_count * 2u;
    const size_t id_range_offsets = id_deltas + (size_t)seg_count * 2u;

    if (!range_is_valid(size, id_range_offsets, (size_t)seg_count * 2u)) {
        return 0;
    }

    for (uint16_t i = 0; i < seg_count; ++i) {
        const uint16_t end_code = read_u16(subtable + end_codes + (size_t)i * 2u);
        const uint16_t start_code = read_u16(subtable + start_codes + (size_t)i * 2u);

        if (codepoint < start_code) {
            return 0;
        }

        if (codepoint > end_code) {
            continue;
        }

        const int16_t delta = read_s16(subtable + id_deltas + (size_t)i * 2u);
        const uint16_t range_offset = read_u16(subtable + id_range_offsets + (size_t)i * 2u);

        if (range_offset == 0) {
            return (uint16_t)((codepoint + (uint32_t)delta) & 0xffffu);
        }

        const size_t glyph_offset =
            id_range_offsets + (size_t)i * 2u + range_offset + ((size_t)codepoint - start_code) * 2u;
        if (!range_is_valid(size, glyph_offset, 2)) {
            return 0;
        }

        const uint16_t glyph = read_u16(subtable + glyph_offset);
        if (glyph == 0) {
            return 0;
        }

        return (uint16_t)((glyph + (uint16_t)delta) & 0xffffu);
    }

    return 0;
}

static uint16_t cmap_format12_glyph(const struct ttf_font *font, uint32_t codepoint) {
    const uint8_t *subtable = font->cmap_subtable;
    const size_t size = font->cmap_subtable_size;

    if (size < 16) {
        return 0;
    }

    const uint32_t group_count = read_u32(subtable + 12);
    if (!range_is_valid(size, 16, (size_t)group_count * 12u)) {
        return 0;
    }

    for (uint32_t i = 0; i < group_count; ++i) {
        const uint8_t *group = subtable + 16u + (size_t)i * 12u;
        const uint32_t start_char = read_u32(group);
        const uint32_t end_char = read_u32(group + 4);

        if (codepoint < start_char) {
            return 0;
        }

        if (codepoint <= end_char) {
            const uint32_t start_glyph = read_u32(group + 8);
            const uint32_t glyph = start_glyph + codepoint - start_char;
            return glyph <= 0xffffu ? (uint16_t)glyph : 0;
        }
    }

    return 0;
}

static uint16_t glyph_for_codepoint(const struct ttf_font *font, uint32_t codepoint) {
    if (font->cmap_subtable == 0 || font->cmap_subtable_size < 2) {
        return 0;
    }

    switch (read_u16(font->cmap_subtable)) {
    case 4:
        return cmap_format4_glyph(font, codepoint);
    case 12:
        return cmap_format12_glyph(font, codepoint);
    default:
        return 0;
    }
}

static bool glyph_bounds(const struct ttf_font *font, uint16_t glyph, size_t *out_offset, size_t *out_size) {
    if (glyph >= font->num_glyphs) {
        return false;
    }

    size_t start = 0;
    size_t end = 0;

    if (font->index_to_loc_format == 0) {
        const size_t loca_offset = (size_t)glyph * 2u;
        start = (size_t)read_u16(font->loca.data + loca_offset) * 2u;
        end = (size_t)read_u16(font->loca.data + loca_offset + 2u) * 2u;
    } else {
        const size_t loca_offset = (size_t)glyph * 4u;
        start = read_u32(font->loca.data + loca_offset);
        end = read_u32(font->loca.data + loca_offset + 4u);
    }

    if (end < start || !range_is_valid(font->glyf.size, start, end - start)) {
        return false;
    }

    *out_offset = start;
    *out_size = end - start;
    return true;
}

static int16_t glyph_advance(const struct ttf_font *font, uint16_t glyph) {
    if (glyph < font->number_of_hmetrics) {
        return (int16_t)read_u16(font->hmtx.data + (size_t)glyph * 4u);
    }

    const size_t last_metric = ((size_t)font->number_of_hmetrics - 1u) * 4u;
    return (int16_t)read_u16(font->hmtx.data + last_metric);
}

static bool append_point(struct glyph_outline *outline, int32_t x, int32_t y, bool on_curve) {
    if (outline->point_count >= MAX_GLYPH_POINTS) {
        return false;
    }

    outline->points[outline->point_count].x = x;
    outline->points[outline->point_count].y = y;
    outline->points[outline->point_count].on_curve = on_curve;
    ++outline->point_count;
    return true;
}

static bool append_outline(struct glyph_outline *dest, const struct glyph_outline *src, int32_t dx, int32_t dy) {
    const size_t point_base = dest->point_count;

    if (dest->point_count + src->point_count > MAX_GLYPH_POINTS ||
        dest->contour_count + src->contour_count > MAX_GLYPH_CONTOURS) {
        return false;
    }

    for (size_t i = 0; i < src->point_count; ++i) {
        if (!append_point(dest, src->points[i].x + dx, src->points[i].y + dy, src->points[i].on_curve)) {
            return false;
        }
    }

    for (size_t i = 0; i < src->contour_count; ++i) {
        dest->contours[dest->contour_count] = (uint16_t)(point_base + src->contours[i]);
        ++dest->contour_count;
    }

    if (src->has_bounds) {
        if (!dest->has_bounds) {
            dest->x_min = (int16_t)(src->x_min + dx);
            dest->x_max = (int16_t)(src->x_max + dx);
            dest->y_min = (int16_t)(src->y_min + dy);
            dest->y_max = (int16_t)(src->y_max + dy);
            dest->has_bounds = true;
        } else {
            const int32_t x_min = src->x_min + dx;
            const int32_t x_max = src->x_max + dx;
            const int32_t y_min = src->y_min + dy;
            const int32_t y_max = src->y_max + dy;
            if (x_min < dest->x_min) {
                dest->x_min = (int16_t)x_min;
            }
            if (x_max > dest->x_max) {
                dest->x_max = (int16_t)x_max;
            }
            if (y_min < dest->y_min) {
                dest->y_min = (int16_t)y_min;
            }
            if (y_max > dest->y_max) {
                dest->y_max = (int16_t)y_max;
            }
        }
    }

    return true;
}

static bool load_glyph_outline(
    const struct ttf_font *font,
    uint16_t glyph,
    struct glyph_outline *outline,
    unsigned int depth
);

static bool load_simple_outline(
    const struct ttf_font *font,
    uint16_t glyph,
    const uint8_t *glyph_data,
    size_t glyph_size,
    struct glyph_outline *outline
) {
    if (glyph_size < 10) {
        return false;
    }

    const int16_t contour_count = read_s16(glyph_data);
    if (contour_count < 0 || contour_count > MAX_GLYPH_CONTOURS) {
        return false;
    }

    outline->x_min = read_s16(glyph_data + 2);
    outline->y_min = read_s16(glyph_data + 4);
    outline->x_max = read_s16(glyph_data + 6);
    outline->y_max = read_s16(glyph_data + 8);
    outline->advance_width = glyph_advance(font, glyph);
    outline->has_bounds = true;

    if (contour_count == 0) {
        return true;
    }

    size_t cursor = 10;
    uint16_t end_points[MAX_GLYPH_CONTOURS];

    if (!range_is_valid(glyph_size, cursor, (size_t)contour_count * 2u)) {
        return false;
    }

    uint16_t last_point = 0;
    for (int16_t i = 0; i < contour_count; ++i) {
        end_points[i] = read_u16(glyph_data + cursor + (size_t)i * 2u);
        if (i != 0 && end_points[i] <= end_points[i - 1]) {
            return false;
        }
        last_point = end_points[i];
    }
    cursor += (size_t)contour_count * 2u;

    const size_t point_count = (size_t)last_point + 1u;
    if (point_count > MAX_GLYPH_POINTS) {
        return false;
    }

    if (!range_is_valid(glyph_size, cursor, 2)) {
        return false;
    }
    const uint16_t instruction_length = read_u16(glyph_data + cursor);
    cursor += 2u;
    if (!range_is_valid(glyph_size, cursor, instruction_length)) {
        return false;
    }
    cursor += instruction_length;

    uint8_t flags[MAX_GLYPH_POINTS];
    size_t flag_index = 0;
    while (flag_index < point_count) {
        if (!range_is_valid(glyph_size, cursor, 1)) {
            return false;
        }

        const uint8_t flag = glyph_data[cursor];
        ++cursor;
        flags[flag_index] = flag;
        ++flag_index;

        if ((flag & GLYPH_REPEAT_FLAG) != 0) {
            if (!range_is_valid(glyph_size, cursor, 1)) {
                return false;
            }

            uint8_t repeat = glyph_data[cursor];
            ++cursor;
            while (repeat != 0 && flag_index < point_count) {
                flags[flag_index] = flag;
                ++flag_index;
                --repeat;
            }

            if (repeat != 0) {
                return false;
            }
        }
    }

    int32_t x = 0;
    for (size_t i = 0; i < point_count; ++i) {
        const uint8_t flag = flags[i];
        if ((flag & GLYPH_X_SHORT_VECTOR) != 0) {
            if (!range_is_valid(glyph_size, cursor, 1)) {
                return false;
            }
            const int32_t delta = glyph_data[cursor];
            ++cursor;
            x += (flag & GLYPH_X_IS_SAME_OR_POSITIVE) != 0 ? delta : -delta;
        } else if ((flag & GLYPH_X_IS_SAME_OR_POSITIVE) == 0) {
            if (!range_is_valid(glyph_size, cursor, 2)) {
                return false;
            }
            x += read_s16(glyph_data + cursor);
            cursor += 2u;
        }

        outline->points[i].x = x;
    }

    int32_t y = 0;
    for (size_t i = 0; i < point_count; ++i) {
        const uint8_t flag = flags[i];
        if ((flag & GLYPH_Y_SHORT_VECTOR) != 0) {
            if (!range_is_valid(glyph_size, cursor, 1)) {
                return false;
            }
            const int32_t delta = glyph_data[cursor];
            ++cursor;
            y += (flag & GLYPH_Y_IS_SAME_OR_POSITIVE) != 0 ? delta : -delta;
        } else if ((flag & GLYPH_Y_IS_SAME_OR_POSITIVE) == 0) {
            if (!range_is_valid(glyph_size, cursor, 2)) {
                return false;
            }
            y += read_s16(glyph_data + cursor);
            cursor += 2u;
        }

        outline->points[i].y = y;
        outline->points[i].on_curve = (flags[i] & GLYPH_ON_CURVE) != 0;
    }

    outline->point_count = point_count;
    outline->contour_count = (size_t)contour_count;
    for (int16_t i = 0; i < contour_count; ++i) {
        outline->contours[i] = end_points[i];
    }

    return true;
}

static bool load_composite_outline(
    const struct ttf_font *font,
    uint16_t glyph,
    const uint8_t *glyph_data,
    size_t glyph_size,
    struct glyph_outline *outline,
    unsigned int depth
) {
    if (glyph_size < 10) {
        return false;
    }

    outline->x_min = read_s16(glyph_data + 2);
    outline->y_min = read_s16(glyph_data + 4);
    outline->x_max = read_s16(glyph_data + 6);
    outline->y_max = read_s16(glyph_data + 8);
    outline->advance_width = glyph_advance(font, glyph);
    outline->has_bounds = true;

    size_t cursor = 10;
    uint16_t flags = 0;

    do {
        if (!range_is_valid(glyph_size, cursor, 4)) {
            return false;
        }

        flags = read_u16(glyph_data + cursor);
        const uint16_t component_glyph = read_u16(glyph_data + cursor + 2u);
        cursor += 4u;

        int32_t arg1 = 0;
        int32_t arg2 = 0;
        if ((flags & ARG_1_AND_2_ARE_WORDS) != 0) {
            if (!range_is_valid(glyph_size, cursor, 4)) {
                return false;
            }
            arg1 = read_s16(glyph_data + cursor);
            arg2 = read_s16(glyph_data + cursor + 2u);
            cursor += 4u;
        } else {
            if (!range_is_valid(glyph_size, cursor, 2)) {
                return false;
            }
            arg1 = (int8_t)glyph_data[cursor];
            arg2 = (int8_t)glyph_data[cursor + 1u];
            cursor += 2u;
        }

        int32_t dx = 0;
        int32_t dy = 0;
        if ((flags & ARGS_ARE_XY_VALUES) != 0) {
            dx = arg1;
            dy = arg2;
        }

        if ((flags & WE_HAVE_A_SCALE) != 0) {
            if (!range_is_valid(glyph_size, cursor, 2)) {
                return false;
            }
            cursor += 2u;
        } else if ((flags & WE_HAVE_AN_X_AND_Y_SCALE) != 0) {
            if (!range_is_valid(glyph_size, cursor, 4)) {
                return false;
            }
            cursor += 4u;
        } else if ((flags & WE_HAVE_A_TWO_BY_TWO) != 0) {
            if (!range_is_valid(glyph_size, cursor, 8)) {
                return false;
            }
            cursor += 8u;
        }

        struct glyph_outline component;
        if (!load_glyph_outline(font, component_glyph, &component, depth + 1u)) {
            return false;
        }

        if (!append_outline(outline, &component, dx, dy)) {
            return false;
        }
    } while ((flags & MORE_COMPONENTS) != 0);

    if ((flags & WE_HAVE_INSTRUCTIONS) != 0) {
        if (!range_is_valid(glyph_size, cursor, 2)) {
            return false;
        }
        const uint16_t instruction_length = read_u16(glyph_data + cursor);
        cursor += 2u;
        if (!range_is_valid(glyph_size, cursor, instruction_length)) {
            return false;
        }
    }

    return true;
}

static bool load_glyph_outline(
    const struct ttf_font *font,
    uint16_t glyph,
    struct glyph_outline *outline,
    unsigned int depth
) {
    if (depth > MAX_COMPONENT_DEPTH) {
        return false;
    }

    memset(outline, 0, sizeof(*outline));
    outline->advance_width = glyph_advance(font, glyph);

    size_t offset = 0;
    size_t size = 0;
    if (!glyph_bounds(font, glyph, &offset, &size)) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    const uint8_t *glyph_data = font->glyf.data + offset;
    if (size < 10) {
        return false;
    }

    const int16_t contour_count = read_s16(glyph_data);
    if (contour_count >= 0) {
        return load_simple_outline(font, glyph, glyph_data, size, outline);
    }

    return load_composite_outline(font, glyph, glyph_data, size, outline, depth);
}

static int32_t scale_design(int32_t value, int32_t scale, int32_t units_per_em) {
    if (value >= 0) {
        return (value * scale + units_per_em / 2) / units_per_em;
    }

    return -((-value * scale + units_per_em / 2) / units_per_em);
}

static int32_t positive_or_default(int32_t value, int32_t fallback) {
    return value > 0 ? value : fallback;
}

static void include_outline_bounds(const struct glyph_outline *outline, int32_t *min_y, int32_t *max_y) {
    if (!outline->has_bounds || outline->point_count == 0) {
        return;
    }

    if (outline->y_min < *min_y) {
        *min_y = outline->y_min;
    }
    if (outline->y_max > *max_y) {
        *max_y = outline->y_max;
    }
}

/*
 * Terminal glyphs are rasterized into a fixed cell, so descenders must be
 * accounted for in the font-wide line box instead of per-glyph cropping.
 */
static bool build_font_metrics(const struct ttf_font *font, struct font_metrics *metrics) {
    int32_t design_top = font->typo_ascent != 0 ? font->typo_ascent : font->ascent;
    int32_t design_bottom = font->typo_descent != 0 ? font->typo_descent : font->descent;
    int32_t design_gap = font->typo_line_gap != 0 ? font->typo_line_gap : font->line_gap;

    if (design_top <= design_bottom) {
        design_top = font->ascent;
        design_bottom = font->descent;
        design_gap = font->line_gap;
    }

    if (design_top <= design_bottom) {
        design_top = (int32_t)font->units_per_em;
        design_bottom = 0;
        design_gap = 0;
    }

    int32_t min_y = design_bottom;
    int32_t max_y = design_top;
    for (uint32_t codepoint = 0x20u; codepoint <= 0x7eu; ++codepoint) {
        const uint16_t glyph = glyph_for_codepoint(font, codepoint);
        if (glyph == 0) {
            continue;
        }

        struct glyph_outline outline;
        if (load_glyph_outline(font, glyph, &outline, 0)) {
            include_outline_bounds(&outline, &min_y, &max_y);
        }
    }

    if (max_y <= min_y) {
        return false;
    }

    const int32_t target_height = (int32_t)XBKS_TTY_FONT_HEIGHT - 4;
    int32_t design_span = max_y - min_y + positive_or_default(design_gap, 0);
    if (design_span <= 0) {
        design_span = max_y - min_y;
    }

    if (target_height <= 0 || design_span <= 0) {
        return false;
    }

    metrics->scale = target_height;
    metrics->design_top = max_y + positive_or_default(design_gap, 0) / 2;
    metrics->design_span = design_span;
    metrics->origin_y = ((int32_t)XBKS_TTY_FONT_HEIGHT - target_height) / 2;
    return true;
}

static struct glyph_point point_midpoint(struct glyph_point lhs, struct glyph_point rhs) {
    struct glyph_point midpoint = {
        .x = (lhs.x + rhs.x) / 2,
        .y = (lhs.y + rhs.y) / 2,
        .on_curve = true,
    };
    return midpoint;
}

static bool add_edge(struct raster_edge *edges, size_t *edge_count, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (y0 == y1) {
        return true;
    }

    if (*edge_count >= MAX_RASTER_EDGES) {
        return false;
    }

    edges[*edge_count].x0 = x0;
    edges[*edge_count].y0 = y0;
    edges[*edge_count].x1 = x1;
    edges[*edge_count].y1 = y1;
    ++(*edge_count);
    return true;
}

static bool add_quadratic(
    struct raster_edge *edges,
    size_t *edge_count,
    struct glyph_point p0,
    struct glyph_point p1,
    struct glyph_point p2
) {
    enum { STEPS = 8 };
    int32_t previous_x = p0.x;
    int32_t previous_y = p0.y;

    for (int i = 1; i <= STEPS; ++i) {
        const int32_t t = i;
        const int32_t mt = STEPS - i;
        const int32_t x = (mt * mt * p0.x + 2 * mt * t * p1.x + t * t * p2.x) / (STEPS * STEPS);
        const int32_t y = (mt * mt * p0.y + 2 * mt * t * p1.y + t * t * p2.y) / (STEPS * STEPS);

        if (!add_edge(edges, edge_count, previous_x, previous_y, x, y)) {
            return false;
        }

        previous_x = x;
        previous_y = y;
    }

    return true;
}

static bool contour_to_edges(
    const struct glyph_outline *outline,
    size_t start,
    size_t end,
    struct raster_edge *edges,
    size_t *edge_count
) {
    if (start > end || end >= outline->point_count) {
        return false;
    }

    const size_t count = end - start + 1u;
    struct glyph_point first = outline->points[start];
    struct glyph_point last = outline->points[end];
    struct glyph_point current;
    size_t index = start;

    if (!first.on_curve) {
        first = last.on_curve ? last : point_midpoint(last, first);
    }

    current = first;
    while (index <= end) {
        struct glyph_point control = outline->points[index];
        struct glyph_point next = outline->points[start + ((index - start + 1u) % count)];

        if (control.on_curve) {
            if (!add_edge(edges, edge_count, current.x, current.y, control.x, control.y)) {
                return false;
            }
            current = control;
            ++index;
            continue;
        }

        if (!next.on_curve) {
            next = point_midpoint(control, next);
        } else {
            ++index;
        }

        if (!add_quadratic(edges, edge_count, current, control, next)) {
            return false;
        }
        current = next;
        ++index;
    }

    return add_edge(edges, edge_count, current.x, current.y, first.x, first.y);
}

static bool point_inside_outline(const struct raster_edge *edges, size_t edge_count, int32_t sample_x, int32_t sample_y) {
    bool inside = false;

    for (size_t i = 0; i < edge_count; ++i) {
        const struct raster_edge *edge = &edges[i];
        int32_t y0 = edge->y0;
        int32_t y1 = edge->y1;
        int32_t x0 = edge->x0;
        int32_t x1 = edge->x1;

        if (y0 > y1) {
            const int32_t temp_y = y0;
            const int32_t temp_x = x0;
            y0 = y1;
            x0 = x1;
            y1 = temp_y;
            x1 = temp_x;
        }

        if (sample_y < y0 || sample_y >= y1) {
            continue;
        }

        const int32_t intersection =
            x0 + (int32_t)(((int64_t)(sample_y - y0) * (x1 - x0)) / (y1 - y0));
        if (sample_x >= intersection) {
            inside = !inside;
        }
    }

    return inside;
}

static uint8_t rasterize_coverage(const struct raster_edge *edges, size_t edge_count, int32_t x, int32_t y) {
    static const int32_t samples[16][2] = {
        { 2, 2 },
        { 6, 2 },
        { 10, 2 },
        { 14, 2 },
        { 2, 6 },
        { 6, 6 },
        { 10, 6 },
        { 14, 6 },
        { 2, 10 },
        { 6, 10 },
        { 10, 10 },
        { 14, 10 },
        { 2, 14 },
        { 6, 14 },
        { 10, 14 },
        { 14, 14 },
    };
    unsigned int covered = 0;

    for (size_t i = 0; i < 16; ++i) {
        if (point_inside_outline(edges, edge_count, x * 16 + samples[i][0], y * 16 + samples[i][1])) {
            ++covered;
        }
    }

    return (uint8_t)((covered * 255u + 8u) / 16u);
}

static bool outline_edges(
    const struct glyph_outline *outline,
    struct raster_edge *edges,
    size_t *edge_count
) {
    size_t start = 0;
    *edge_count = 0;

    for (size_t i = 0; i < outline->contour_count; ++i) {
        const size_t end = outline->contours[i];
        if (!contour_to_edges(outline, start, end, edges, edge_count)) {
            return false;
        }
        start = end + 1u;
    }

    return true;
}

static bool scale_outline_to_cell(
    const struct ttf_font *font,
    const struct glyph_outline *source,
    const struct font_metrics *metrics,
    struct glyph_outline *scaled
) {
    int32_t advance = source->advance_width;

    if (metrics->design_span <= 0 ||
        metrics->scale <= 0 ||
        source->point_count > MAX_GLYPH_POINTS ||
        source->contour_count > MAX_GLYPH_CONTOURS) {
        return false;
    }

    if (advance <= 0) {
        advance = font->units_per_em / 2;
    }

    int32_t glyph_width = scale_design(advance, metrics->scale, metrics->design_span);
    if (glyph_width <= 0) {
        glyph_width = 1;
    }

    if (glyph_width > (int32_t)XBKS_TTY_FONT_WIDTH - 1) {
        glyph_width = (int32_t)XBKS_TTY_FONT_WIDTH - 1;
    }

    const int32_t offset_x = ((int32_t)XBKS_TTY_FONT_WIDTH - glyph_width) / 2;

    memset(scaled, 0, sizeof(*scaled));
    scaled->point_count = source->point_count;
    scaled->contour_count = source->contour_count;
    scaled->advance_width = (int16_t)glyph_width;
    scaled->has_bounds = source->has_bounds;
    scaled->x_min = source->x_min;
    scaled->x_max = source->x_max;
    scaled->y_min = source->y_min;
    scaled->y_max = source->y_max;

    for (size_t i = 0; i < source->point_count; ++i) {
        scaled->points[i].x =
            (offset_x + scale_design(source->points[i].x, metrics->scale, metrics->design_span)) * 16;
        scaled->points[i].y =
            (metrics->origin_y +
                scale_design(metrics->design_top - source->points[i].y, metrics->scale, metrics->design_span)) * 16;
        scaled->points[i].on_curve = source->points[i].on_curve;
    }

    for (size_t i = 0; i < source->contour_count; ++i) {
        scaled->contours[i] = source->contours[i];
    }

    return true;
}

static bool rasterize_glyph(
    const struct ttf_font *font,
    const struct font_metrics *metrics,
    uint32_t codepoint,
    uint8_t *out_rows
) {
    memset(out_rows, 0, XBKS_TTY_FONT_WIDTH * XBKS_TTY_FONT_HEIGHT);

    if (codepoint == ' ') {
        return true;
    }

    const uint16_t glyph = glyph_for_codepoint(font, codepoint);
    if (glyph == 0) {
        return false;
    }

    struct glyph_outline outline;
    struct glyph_outline scaled;
    struct raster_edge edges[MAX_RASTER_EDGES];
    size_t edge_count = 0;

    if (!load_glyph_outline(font, glyph, &outline, 0) ||
        outline.point_count == 0 ||
        !scale_outline_to_cell(font, &outline, metrics, &scaled) ||
        !outline_edges(&scaled, edges, &edge_count)) {
        return false;
    }

    for (size_t y = 0; y < XBKS_TTY_FONT_HEIGHT; ++y) {
        for (size_t x = 0; x < XBKS_TTY_FONT_WIDTH; ++x) {
            out_rows[y * XBKS_TTY_FONT_WIDTH + x] =
                rasterize_coverage(edges, edge_count, (int32_t)x, (int32_t)y);
        }
    }

    return true;
}

static void draw_missing_glyph(uint8_t *rows) {
    memset(rows, 0, XBKS_TTY_FONT_WIDTH * XBKS_TTY_FONT_HEIGHT);

    for (size_t x = 2; x < XBKS_TTY_FONT_WIDTH - 2u; ++x) {
        rows[3 * XBKS_TTY_FONT_WIDTH + x] = 220;
        rows[(XBKS_TTY_FONT_HEIGHT - 4u) * XBKS_TTY_FONT_WIDTH + x] = 220;
    }

    for (size_t y = 3; y < XBKS_TTY_FONT_HEIGHT - 2u; ++y) {
        rows[y * XBKS_TTY_FONT_WIDTH + 2u] = 220;
        rows[y * XBKS_TTY_FONT_WIDTH + XBKS_TTY_FONT_WIDTH - 3u] = 220;
    }
}

bool xbks_font_load_system_tty(const struct limine_module_response *modules, struct xbks_bitmap_font *out) {
    if (out == 0) {
        return false;
    }

    const struct limine_file *module = find_font_module(modules);
    if (module == 0) {
        xbks_log_write(XBKS_LOG_WARN, "system TTF module not found; using built-in terminal font");
        return false;
    }

    struct ttf_font font;
    if (!parse_ttf(module->address, (size_t)module->size, &font)) {
        xbks_log_write(XBKS_LOG_WARN, "system TTF parse failed; using built-in terminal font");
        return false;
    }

    struct font_metrics metrics;
    if (!build_font_metrics(&font, &metrics)) {
        xbks_log_write(XBKS_LOG_WARN, "system TTF metrics failed; using built-in terminal font");
        return false;
    }

    size_t rendered = 0;
    char failed_ascii[96];
    size_t failed_ascii_count = 0;

    for (uint32_t codepoint = 0; codepoint < XBKS_TTY_FONT_GLYPHS; ++codepoint) {
        uint8_t *rows = &system_font_alpha[codepoint * XBKS_TTY_FONT_WIDTH * XBKS_TTY_FONT_HEIGHT];

        if (rasterize_glyph(&font, &metrics, codepoint, rows)) {
            ++rendered;
        } else {
            if (codepoint >= 0x21u && codepoint <= 0x7eu && failed_ascii_count + 1u < sizeof(failed_ascii)) {
                failed_ascii[failed_ascii_count] = (char)codepoint;
                ++failed_ascii_count;
            }
            draw_missing_glyph(rows);
        }
    }

    failed_ascii[failed_ascii_count] = '\0';

    out->alpha = system_font_alpha;
    out->glyphs = XBKS_TTY_FONT_GLYPHS;
    out->width = XBKS_TTY_FONT_WIDTH;
    out->height = XBKS_TTY_FONT_HEIGHT;
    out->source = module->path != 0 ? module->path : "system-font";

    xbks_log_printf(
        XBKS_LOG_INFO,
        "system TTF terminal font loaded: %zu glyphs, cell=%zux%zu",
        rendered,
        (size_t)XBKS_TTY_FONT_WIDTH,
        (size_t)XBKS_TTY_FONT_HEIGHT
    );
    if (failed_ascii_count != 0) {
        xbks_log_printf(XBKS_LOG_WARN, "system TTF missing ASCII glyphs: %s", failed_ascii);
    }
    return true;
}
