#pragma once

#include "main.h"

typedef enum {
    UP,
    DOWN,
    LEFT,
    RIGHT
} Direction;

void snake_init(void);
void snake_update(void);
void snake_draw(void);
void snake_handle_input(Key key);