/*
 * 2D Graphics Editor in C using ncurses
 * Draws shapes using '*' and '_' on a 2D character canvas
 * Shapes: Circle, Rectangle, Line, Triangle
 * Ops:    Add, Delete, Modify, List, Clear
 *
 * Compile:  gcc -o graphics_editor graphics_editor.c -lncurses -lm
 * Run:      ./graphics_editor
 */

#ifdef _WIN32
  #include <ncurses/ncurses.h>
#else
  #include <ncurses.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ──────────────────────────────────────────────────────────────────
 *  Constants
 * ────────────────────────────────────────────────────────────────── */
#define CANVAS_ROWS  30
#define CANVAS_COLS  80
#define MAX_OBJECTS  50

#define SHAPE_CIRCLE   0
#define SHAPE_RECT     1
#define SHAPE_LINE     2
#define SHAPE_TRIANGLE 3

/* ──────────────────────────────────────────────────────────────────
 *  Data structure for a single shape object
 * ────────────────────────────────────────────────────────────────── */
typedef struct {
    int type;          /* SHAPE_* constant                        */
    int x1, y1;        /* centre / top-left / start / vertex-1   */
    int x2, y2;        /* radius / bottom-right / end / vertex-2 */
    int x3, y3;        /* third vertex (triangle only)            */
    int active;        /* 1 = visible, 0 = deleted                */
} Object;

/* ──────────────────────────────────────────────────────────────────
 *  Global state
 * ────────────────────────────────────────────────────────────────── */
static char   canvas[CANVAS_ROWS][CANVAS_COLS];
static Object objects[MAX_OBJECTS];
static int    obj_count = 0;

static WINDOW *canvas_win;  /* picture display          */
static WINDOW *menu_win;    /* menus & input prompts    */

/* ──────────────────────────────────────────────────────────────────
 *  Canvas helpers
 * ────────────────────────────────────────────────────────────────── */
static void canvas_clear(void)
{
    int r, c;
    for (r = 0; r < CANVAS_ROWS; r++)
        for (c = 0; c < CANVAS_COLS; c++)
            canvas[r][c] = ' ';
}

static void canvas_put(int row, int col, char ch)
{
    if (row >= 0 && row < CANVAS_ROWS && col >= 0 && col < CANVAS_COLS)
        canvas[row][col] = ch;
}

/* ──────────────────────────────────────────────────────────────────
 *  Shape Drawing Functions
 * ────────────────────────────────────────────────────────────────── */

/*  Circle – border '*', interior '_'
 *  Terminal cells are ~twice as tall as wide, so we scale x by 2. */
static void draw_circle(int cx, int cy, int r)
{
    int row, col;
    for (row = cy - r; row <= cy + r; row++) {
        for (col = cx - 2*r; col <= cx + 2*r; col++) {
            double dx   = (col - cx) / 2.0;
            double dy   = (double)(row - cy);
            double dist = sqrt(dx*dx + dy*dy);
            if (fabs(dist - r) < 0.7)
                canvas_put(row, col, '*');
            else if (dist < r - 0.6)
                canvas_put(row, col, '_');
        }
    }
}

/*  Rectangle – border '*', interior '_' */
static void draw_rectangle(int x1, int y1, int x2, int y2)
{
    int r, c;
    int rmin = y1, rmax = y2, cmin = x1, cmax = x2;
    if (rmin > rmax) { int t = rmin; rmin = rmax; rmax = t; }
    if (cmin > cmax) { int t = cmin; cmin = cmax; cmax = t; }
    for (r = rmin; r <= rmax; r++)
        for (c = cmin; c <= cmax; c++)
            canvas_put(r, c,
                (r==rmin || r==rmax || c==cmin || c==cmax) ? '*' : '_');
}

/*  Line – Bresenham '*' */
static void draw_line(int x1, int y1, int x2, int y2)
{
    int dx = abs(x2 - x1), dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    for (;;) {
        canvas_put(y1, x1, '*');
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x1 += sx; }
        if (e2 <  dx) { err += dx; y1 += sy; }
    }
}

/*  Triangle scan-fill helper */
static int tri_lerp(int row, int ra, int ca, int rb, int cb)
{
    if (rb == ra) return ca;
    return ca + (cb - ca) * (row - ra) / (rb - ra);
}

static void fill_span(int row, int ca, int cb)
{
    int c;
    if (ca > cb) { int t = ca; ca = cb; cb = t; }
    for (c = ca; c <= cb; c++)
        canvas_put(row, c, (c == ca || c == cb) ? '*' : '_');
}

/*  Triangle – filled, edges '*', interior '_' */
static void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3)
{
    int row, a, b, t;
    /* Sort vertices top-to-bottom */
    if (y1 > y2) { t=x1;x1=x2;x2=t; t=y1;y1=y2;y2=t; }
    if (y1 > y3) { t=x1;x1=x3;x3=t; t=y1;y1=y3;y3=t; }
    if (y2 > y3) { t=x2;x2=x3;x3=t; t=y2;y2=y3;y3=t; }

    for (row = y1; row <= y3; row++) {
        a = tri_lerp(row, y1, x1, y3, x3);
        b = (row <= y2) ? tri_lerp(row, y1, x1, y2, x2)
                        : tri_lerp(row, y2, x2, y3, x3);
        fill_span(row, a, b);
    }
    /* Overdraw edges */
    draw_line(x1,y1, x2,y2);
    draw_line(x2,y2, x3,y3);
    draw_line(x3,y3, x1,y1);
}

/* ──────────────────────────────────────────────────────────────────
 *  Redraw all active objects onto the canvas
 * ────────────────────────────────────────────────────────────────── */
static void redraw_canvas(void)
{
    int i;
    canvas_clear();
    for (i = 0; i < obj_count; i++) {
        Object *o = &objects[i];
        if (!o->active) continue;
        switch (o->type) {
            case SHAPE_CIRCLE:
                draw_circle(o->x1, o->y1, o->x2);        break;
            case SHAPE_RECT:
                draw_rectangle(o->x1, o->y1, o->x2, o->y2); break;
            case SHAPE_LINE:
                draw_line(o->x1, o->y1, o->x2, o->y2);   break;
            case SHAPE_TRIANGLE:
                draw_triangle(o->x1, o->y1, o->x2, o->y2, o->x3, o->y3); break;
        }
    }
}

/* ──────────────────────────────────────────────────────────────────
 *  Display canvas in ncurses window
 * ────────────────────────────────────────────────────────────────── */
static void display_canvas(void)
{
    int r, c;
    werase(canvas_win);
    box(canvas_win, 0, 0);
    mvwprintw(canvas_win, 0, 2, " 2D Graphics Editor ");
    for (r = 0; r < CANVAS_ROWS; r++)
        for (c = 0; c < CANVAS_COLS; c++) {
            char ch = canvas[r][c];
            if (ch != ' ')
                mvwaddch(canvas_win, r + 1, c + 1, ch);
        }
    wrefresh(canvas_win);
}

/* ──────────────────────────────────────────────────────────────────
 *  Menu / input helpers
 * ────────────────────────────────────────────────────────────────── */
static void menu_msg_row(int row, const char *msg)
{
    wmove(menu_win, row, 1);
    wclrtoeol(menu_win);
    mvwprintw(menu_win, row, 1, "%s", msg);
    wrefresh(menu_win);
}

static int menu_getint(int row, const char *prompt)
{
    char buf[32] = {0};
    echo();
    mvwprintw(menu_win, row, 1, "%-28s: ", prompt);
    wrefresh(menu_win);
    wgetnstr(menu_win, buf, sizeof(buf) - 1);
    noecho();
    return atoi(buf);
}

/* Wipe the prompt area (rows 2–11) */
static void menu_clear_area(void)
{
    int r;
    for (r = 2; r <= 11; r++) {
        wmove(menu_win, r, 1);
        wclrtoeol(menu_win);
    }
    wrefresh(menu_win);
}

/* ──────────────────────────────────────────────────────────────────
 *  Object operations: Add / List / Delete / Modify
 * ────────────────────────────────────────────────────────────────── */
static void op_add(int type)
{
    Object o;
    if (obj_count >= MAX_OBJECTS) {
        menu_msg_row(3, "Object limit reached (50)!");
        return;
    }
    memset(&o, 0, sizeof(o));
    o.type   = type;
    o.active = 1;

    menu_clear_area();
    switch (type) {
        case SHAPE_CIRCLE:
            menu_msg_row(2, "--- Add Circle ---");
            o.x1 = menu_getint(3, "Centre col  (0-79)");
            o.y1 = menu_getint(4, "Centre row  (0-29)");
            o.x2 = menu_getint(5, "Radius");
            break;
        case SHAPE_RECT:
            menu_msg_row(2, "--- Add Rectangle ---");
            o.x1 = menu_getint(3, "Top-left col  x1");
            o.y1 = menu_getint(4, "Top-left row  y1");
            o.x2 = menu_getint(5, "Bot-right col x2");
            o.y2 = menu_getint(6, "Bot-right row y2");
            break;
        case SHAPE_LINE:
            menu_msg_row(2, "--- Add Line ---");
            o.x1 = menu_getint(3, "Start col x1");
            o.y1 = menu_getint(4, "Start row y1");
            o.x2 = menu_getint(5, "End col   x2");
            o.y2 = menu_getint(6, "End row   y2");
            break;
        case SHAPE_TRIANGLE:
            menu_msg_row(2, "--- Add Triangle ---");
            o.x1 = menu_getint(3, "Vertex-1 col x1");
            o.y1 = menu_getint(4, "Vertex-1 row y1");
            o.x2 = menu_getint(5, "Vertex-2 col x2");
            o.y2 = menu_getint(6, "Vertex-2 row y2");
            o.x3 = menu_getint(7, "Vertex-3 col x3");
            o.y3 = menu_getint(8, "Vertex-3 row y3");
            break;
    }
    objects[obj_count++] = o;
    menu_msg_row(10, "Object added. Press any key...");
    wgetch(menu_win);
}

static void op_list(void)
{
    int i, row = 3;
    const char *names[] = {"Circle","Rectangle","Line","Triangle"};
    menu_clear_area();
    menu_msg_row(2, "--- Object List ---");
    for (i = 0; i < obj_count && row <= 11; i++) {
        if (!objects[i].active) continue;
        Object *o = &objects[i];
        mvwprintw(menu_win, row++, 1, "[%2d] %-9s (%2d,%2d)(%2d,%2d)",
                  i, names[o->type], o->x1, o->y1, o->x2, o->y2);
    }
    if (row == 3) menu_msg_row(3, "(no objects)");
    wrefresh(menu_win);
}

static void op_delete(void)
{
    int id;
    op_list();
    id = menu_getint(11, "Delete object #");
    if (id >= 0 && id < obj_count && objects[id].active) {
        objects[id].active = 0;
        menu_msg_row(10, "Deleted. Press any key...");
    } else {
        menu_msg_row(10, "Invalid id. Press any key...");
    }
    wgetch(menu_win);
}

static void op_modify(void)
{
    int id, type;
    op_list();
    id = menu_getint(11, "Modify object #");
    if (id < 0 || id >= obj_count || !objects[id].active) {
        menu_msg_row(10, "Invalid id. Press any key...");
        wgetch(menu_win);
        return;
    }
    type              = objects[id].type;
    objects[id].active = 0;  /* logically delete old    */
    obj_count         = id;   /* reuse the same slot     */
    op_add(type);
}

/* ──────────────────────────────────────────────────────────────────
 *  Main menu display
 * ────────────────────────────────────────────────────────────────── */
static void draw_main_menu(void)
{
    werase(menu_win);
    box(menu_win, 0, 0);
    mvwprintw(menu_win,  0, 2, " Menu ");
    mvwprintw(menu_win,  1, 2, "1) Add Circle");
    mvwprintw(menu_win,  2, 2, "2) Add Rectangle");
    mvwprintw(menu_win,  3, 2, "3) Add Line");
    mvwprintw(menu_win,  4, 2, "4) Add Triangle");
    mvwprintw(menu_win,  5, 2, "5) List objects");
    mvwprintw(menu_win,  6, 2, "6) Delete object");
    mvwprintw(menu_win,  7, 2, "7) Modify object");
    mvwprintw(menu_win,  8, 2, "8) Clear canvas");
    mvwprintw(menu_win,  9, 2, "q) Quit");
    mvwprintw(menu_win, 10, 2, "-------------------");
    mvwprintw(menu_win, 11, 2, "Choice: ");
    wrefresh(menu_win);
}

/* ──────────────────────────────────────────────────────────────────
 *  main()
 * ────────────────────────────────────────────────────────────────── */
int main(void)
{
    int ch;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(1);

    /* canvas window: rows 0..31, cols 0..81 */
    canvas_win = newwin(CANVAS_ROWS + 2, CANVAS_COLS + 2, 0, 0);

    /* menu window: to the right of canvas */
    menu_win   = newwin(14, 36, 0, CANVAS_COLS + 3);

    canvas_clear();
    display_canvas();
    draw_main_menu();

    while ((ch = mvwgetch(menu_win, 11, 10)) != 'q' && ch != 'Q') {
        switch (ch) {
            case '1': op_add(SHAPE_CIRCLE);    break;
            case '2': op_add(SHAPE_RECT);      break;
            case '3': op_add(SHAPE_LINE);      break;
            case '4': op_add(SHAPE_TRIANGLE);  break;
            case '5': op_list(); wgetch(menu_win); break;
            case '6': op_delete();             break;
            case '7': op_modify();             break;
            case '8': obj_count = 0;           break;
            default:  break;
        }
        redraw_canvas();
        display_canvas();
        draw_main_menu();
    }

    endwin();
    printf("Goodbye!\n");
    return 0;
}
