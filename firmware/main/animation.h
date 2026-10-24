#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    uint16_t width, height, fps;
    uint32_t count;
} bober_animation_t;

bool bober_animation_open(bober_animation_t *anim, const uint8_t *data, size_t size);
/* Output: RGB565 in panel wire byte order (high byte first). */
bool bober_animation_decode(const bober_animation_t *anim, uint32_t frame,
                            uint8_t *rgb565, size_t capacity);
/* Shift a tile, extending edge pixels so tiny motions do not expose black bars. */
bool bober_picture_rows(const uint8_t *frame, uint8_t *tile, size_t capacity,
                        unsigned first_row, unsigned rows, int dx, int dy);
/* Rotate clockwise by 0..3 quarter turns; motion follows image axes. */
bool bober_picture_rows_rotated(const uint8_t *frame, uint8_t *tile, size_t capacity,
                                unsigned first_row, unsigned rows, int dx, int dy,
                                unsigned rotation);
