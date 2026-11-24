#include <stdint.h>
#include <stdbool.h>
#include <generated/csr.h>
#include <generated/soc.h>
#include "../irq.h"

// ─── 1. UART UTILITIES ───
void uart_write(uint8_t c) {
    while (uart_txfull_read()) {}
    uart_rxtx_write(c);
}

uint8_t uart_read(void) {
    while (uart_rxempty_read()) {}
    return (uint8_t)uart_rxtx_read();
}

int uart_available(void) {
    return !uart_rxempty_read();
}

void print_str(const char *s) {
    while (*s) {
        if (*s == '\n') uart_write('\r');
        uart_write(*s++);
    }
}

void print_int(int val) {
    char buf[16];
    int i = 0;
    if (val == 0) {
        uart_write('0');
        return;
    }
    if (val < 0) {
        uart_write('-');
        val = -val;
    }
    while (val > 0) {
        buf[i++] = (val % 10) + '0';
        val /= 10;
    }
    while (i > 0) {
        uart_write(buf[--i]);
    }
}

// ─── 2. TIMER & IRQ ───
volatile uint32_t system_ticks = 0;
volatile uint8_t game_tick_flag = 0;

void isr(void) {
    // Timer0 (1ms system tick)
    if (timer0_ev_pending_read() & 1) {
        system_ticks++;
        // Trigger a game tick every 100ms for a much faster/smoother game
        if (system_ticks % 100 == 0) {
            game_tick_flag = 1;
        }
        timer0_ev_pending_write(1);
    }
}

void system_init(void) {
    uint32_t ticks_per_ms = CONFIG_CLOCK_FREQUENCY / 1000;
    timer0_en_write(0);                 
    timer0_load_write(0);               
    timer0_reload_write(ticks_per_ms);  
    timer0_ev_pending_write(timer0_ev_pending_read());
    timer0_ev_enable_write(1);
    timer0_en_write(1); 

    irq_setmask(1 << TIMER0_INTERRUPT);
    irq_setie(1);
}

// ─── 3. SNAKE GAME LOGIC ───
#define WIDTH 38
#define HEIGHT 18
#define MAX_SNAKE (WIDTH * HEIGHT)

typedef struct {
    int x, y;
} Point;

Point snake[MAX_SNAKE];
int snake_len;
Point apple;
int dir; // 0=Up, 1=Down, 2=Left, 3=Right
int score;
bool game_over;

// Simple Pseudo-Random Number Generator
uint32_t xorshift32(uint32_t state) {
    static uint32_t x = 123456789;
    if (state) x = state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

void spawn_apple() {
    bool valid;
    do {
        valid = true;
        apple.x = xorshift32(0) % WIDTH;
        apple.y = xorshift32(0) % HEIGHT;
        for (int i = 0; i < snake_len; i++) {
            if (snake[i].x == apple.x && snake[i].y == apple.y) {
                valid = false;
                break;
            }
        }
    } while (!valid);
}

void reset_game() {
    snake_len = 3;
    snake[0].x = WIDTH / 2; snake[0].y = HEIGHT / 2;
    snake[1].x = WIDTH / 2; snake[1].y = HEIGHT / 2 + 1;
    snake[2].x = WIDTH / 2; snake[2].y = HEIGHT / 2 + 2;
    dir = 0; // Moving Up
    score = 0;
    game_over = false;
    xorshift32(system_ticks ? system_ticks : 123456789); // Seed PRNG
    spawn_apple();
    print_str("\033[2J"); // Clear screen to avoid artifacts from welcome screen
}

void draw_cell_ansi(int x, int y, const char* str) {
    int line = y + 4;
    int col = (x + 1) * 2 + 1;
    uart_write('\033');
    uart_write('[');
    print_int(line);
    uart_write(';');
    print_int(col);
    uart_write('H');
    print_str(str);
}

void update_score_ansi() {
    uart_write('\033');
    print_str("[2;8H");
    print_int(score);
    // Park cursor out of the way
    uart_write('\033');
    print_str("[24;1H");
}

void draw_screen() {
    // Move cursor to top-left WITHOUT clearing the screen to prevent flicker
    print_str("\033[H");
    
    print_str("\033[0m=== RISC-V SNAKE ===                  \n");
    print_str("\033[0mScore: ");
    print_int(score);
    print_str("        \n"); // pad with spaces to overwrite old text
    
    for (int y = -1; y <= HEIGHT; y++) {
        for (int x = -1; x <= WIDTH; x++) {
            if (y == -1 || y == HEIGHT || x == -1 || x == WIDTH) {
                print_str("\033[44m  \033[0m"); // Blue wall
            } else if (x == apple.x && y == apple.y) {
                print_str("\033[41m  \033[0m"); // Red apple
            } else {
                bool is_snake = false;
                for (int i = 0; i < snake_len; i++) {
                    if (snake[i].x == x && snake[i].y == y) {
                        is_snake = true;
                        // White block for snake body and head
                        print_str("\033[47m  \033[0m"); 
                        break;
                    }
                }
                if (!is_snake) print_str("  "); // empty space
            }
        }
        print_str("\n");
    }
    
    if (game_over) {
        print_str("\033[0m\nGAME OVER! Press any button to restart.          \n");
    } else {
        print_str("\033[0m\nUse Physical Buttons to move.                    \n");
    }
    // Park cursor at bottom
    uart_write('\033');
    print_str("[24;1H");
}

void read_input() {
    uint32_t btns = portd_in_read();
    
    // bit 0 (0x1) -> Up
    // bit 1 (0x2) -> Down
    // bit 2 (0x4) -> Left
    // bit 3 (0x8) -> Right
    if ((btns & 0x1) && dir != 1) dir = 0; // Up
    if ((btns & 0x2) && dir != 0) dir = 1; // Down
    if ((btns & 0x4) && dir != 3) dir = 2; // Left
    if ((btns & 0x8) && dir != 2) dir = 3; // Right
    
    // Optional: Also allow UART input for testing
    if (uart_available()) {
        char c = uart_read();
        if (c == 'w' && dir != 1) dir = 0;
        if (c == 's' && dir != 0) dir = 1;
        if (c == 'a' && dir != 3) dir = 2;
        if (c == 'd' && dir != 2) dir = 3;
    }
}

void update_game() {
    if (game_over) return;
    
    Point next = snake[0];
    if (dir == 0) next.y--;
    if (dir == 1) next.y++;
    if (dir == 2) next.x--;
    if (dir == 3) next.x++;
    
    // Wall collision
    if (next.x < 0 || next.x >= WIDTH || next.y < 0 || next.y >= HEIGHT) {
        game_over = true;
        return;
    }
    
    // Self collision
    for (int i = 0; i < snake_len - 1; i++) {
        if (snake[i].x == next.x && snake[i].y == next.y) {
            game_over = true;
            return;
        }
    }
    
    Point old_tail = snake[snake_len - 1];
    bool ate = false;
    
    // Move snake
    for (int i = snake_len - 1; i > 0; i--) {
        snake[i] = snake[i - 1];
    }
    snake[0] = next;
    
    // Apple collision
    if (next.x == apple.x && next.y == apple.y) {
        if (snake_len < MAX_SNAKE) {
            snake_len++;
            snake[snake_len - 1] = old_tail; // grow tail momentarily
        }
        score += 10;
        spawn_apple();
        ate = true;
    }
    
    // --- Delta Rendering ---
    if (!ate) {
        draw_cell_ansi(old_tail.x, old_tail.y, "  ");
    }
    draw_cell_ansi(snake[0].x, snake[0].y, "\033[47m  \033[0m");
    if (ate) {
        draw_cell_ansi(apple.x, apple.y, "\033[41m  \033[0m");
        update_score_ansi();
    }
    // Park cursor at bottom
    uart_write('\033');
    print_str("[24;1H");
}

int main(void) {
    system_init();
    
    // Hide cursor and Welcome screen
    print_str("\033[?25l"); // Hide cursor
    print_str("\033[2J\033[H");
    print_str("============================\n");
    print_str("      RISC-V SNAKE\n");
    print_str("============================\n");
    print_str("Press any button to start!\n");
    
    // Wait for any button press to start
    while (portd_in_read() == 0 && !uart_available()) {}
    if (uart_available()) uart_read(); // clear buffer
    
    reset_game();
    draw_screen();
    
    while(1) {
        read_input();
        
        if (game_tick_flag) {
            game_tick_flag = 0;
            
            if (game_over) {
                // If game over, pressing any button restarts
                if (portd_in_read() != 0) {
                    reset_game();
                    draw_screen();
                }
            } else {
                update_game();
                if (game_over) {
                    // Died this tick, redraw to show GAME OVER message
                    draw_screen();
                }
            }
        }
    }
    return 0;
}
