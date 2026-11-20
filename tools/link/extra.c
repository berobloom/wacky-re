/* extra.c: C functions of the game module that src/ww.c leaves out on purpose, compiled apart for
 * the hybrid link (tools/link/build.sh). Defined in ww.c, -oe would inline them at every call site,
 * where the game calls them (analysis/ww_names.txt fn_1c068 / fn_1c0ac); in their own file nothing
 * can inline them. Bodies from the game's code 0x1c068-0x1c0f0. */
extern unsigned short page_draw, page_from, page_to, page_shown;
extern unsigned short save_a, save_b, save_c;
void page_copy(void);

/* ---- 0x1c068: keep the page offsets, show page page_shown through 48000, tail jmp page_copy */
void fn_1c068(void)
{
    save_a = page_draw;
    save_b = page_from;
    save_c = page_to;
    page_to = 48000;
    page_draw = page_shown;
    page_from = page_shown;
    page_copy();
}

/* ---- 0x1c0ac: the reverse - page_from = 48000, page_draw = page_to = page_shown, page_copy, then restore */
void fn_1c0ac(void)
{
    page_from = 48000;
    page_draw = page_shown;
    page_to = page_shown;
    page_copy();
    page_from = save_b;
    page_to = save_c;
    page_draw = save_a;
}
