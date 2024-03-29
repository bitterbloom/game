#pragma once

#include <stdint.h>

typedef struct {
    uint32_t x;
    uint32_t y;
} point;

typedef struct {
    uint32_t id;
    point pos;
} Player;


