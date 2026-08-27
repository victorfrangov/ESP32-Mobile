#include "snake.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "main.h"
#include "u8g2.h"
#include <stdint.h>
#include <stdlib.h>

#define BLOCK_SIZE 4
#define GRID_W (128 / BLOCK_SIZE) // 32 cells
#define GRID_H (64 / BLOCK_SIZE) // 16 cells
#define MAX_SNAKE_LEN 100

typedef struct {
    int x; 
    int y;
} Point; // Keep track of all snake bodies

static Point snake[MAX_SNAKE_LEN];
static int snake_len = 1;
static Direction snake_dir = RIGHT;
static Direction next_dir = RIGHT; // Buffered input
static int sub_step = 0;

static uint32_t last_snake_tick = 0;

static int food_x = 0;
static int food_y = 0;

void spawn_food(void){
    food_x = rand() % GRID_W;
    food_y = rand() % GRID_H;
}

void snake_init(void){
    snake_len = 1;
    snake_dir = RIGHT;
    next_dir = RIGHT;
    sub_step = 0;

    int start_x = GRID_W / 4;
    int start_y = GRID_H / 2;

    // Head
    snake[0].x = start_x;
    snake[0].y = start_y;

    // Initial body
    snake[1].x = start_x - 1;
    snake[1].y = start_y;
    snake[2].x = start_x - 2;
    snake[2].y = start_y;

    spawn_food();
}

void snake_handle_input(Key key){
    if (key == KEY_UP && snake_dir != DOWN) next_dir = UP;
    if (key == KEY_LEFT && snake_dir != RIGHT) next_dir = LEFT;
    if (key == KEY_RIGHT && snake_dir != LEFT) next_dir = RIGHT;
    if (key == KEY_DOWN && snake_dir != UP) next_dir = DOWN;
}

void snake_update(void){
    uint32_t now = xTaskGetTickCount();
    if (now - last_snake_tick < pdMS_TO_TICKS(23)) return;
    last_snake_tick = now;

    sub_step++;

    if (sub_step >= BLOCK_SIZE){
        sub_step = 0;
        snake_dir = next_dir;

        for (int i = snake_len - 1; i > 0; i--){
            snake[i] = snake[i - 1];
        }

        if (snake_dir == RIGHT)     snake[0].x++;
        if (snake_dir == LEFT)      snake[0].x--;
        if (snake_dir == UP)        snake[0].y--;
        if (snake_dir == DOWN)      snake[0].y++;

        if (snake[0].x >= GRID_W)       snake[0].x = 0;
        if (snake[0].x < 0)             snake[0].x = GRID_W - 1;
        if (snake[0].y >= GRID_H)       snake[0].y = 0;
        if (snake[0].y < 0)             snake[0].y = GRID_H - 1;

        if (snake[0].x == food_x && snake[0].y == food_y){
            if (snake_len < MAX_SNAKE_LEN){
                snake[snake_len] = snake[snake_len - 1];
                snake_len++;
            }
            spawn_food();
        }
    }
}

void snake_draw(void){
    u8g2_ClearBuffer(&u8g2);

    u8g2_DrawBox(&u8g2, food_x * BLOCK_SIZE, food_y * BLOCK_SIZE, BLOCK_SIZE, BLOCK_SIZE); // Food

    for (int i = 0; i < snake_len; i++){
        int draw_x = snake[i].x * BLOCK_SIZE;
        int draw_y = snake[i].y * BLOCK_SIZE;
        if (i == 0) {
            // Head slides in current snake_dir
            if (snake_dir == RIGHT) draw_x += sub_step;
            if (snake_dir == LEFT)  draw_x -= sub_step;
            if (snake_dir == DOWN)  draw_y += sub_step;
            if (snake_dir == UP)    draw_y -= sub_step;
        } else {
            // Body segment i slides towards segment (i - 1)
            int dx = snake[i - 1].x - snake[i].x;
            int dy = snake[i - 1].y - snake[i].y;
            // Handle wrapping difference
            if (dx > 1)  dx = -1;
            if (dx < -1) dx = 1;
            if (dy > 1)  dy = -1;
            if (dy < -1) dy = 1;
            draw_x += dx * sub_step;
            draw_y += dy * sub_step;
        }
        // Draw segment
        u8g2_DrawBox(&u8g2, draw_x, draw_y, BLOCK_SIZE, BLOCK_SIZE);
    }
    u8g2_SendBuffer(&u8g2);
}