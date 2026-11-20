/* extra.c: C functions of the game module that src/ww.c leaves out on purpose, compiled apart for
 * the hybrid link (tools/link/build.sh). Defined in ww.c, -oe would inline them at every call site,
 * where the game calls them (analysis/ww_names.txt fn_1c068 / fn_1c0ac); in their own file nothing
 * can inline them. Bodies from the game's code 0x1c068-0x1c0f0. */
extern unsigned short pal_a, pal_b, pal_c, pal_d;
extern unsigned short save_a, save_b, save_c;
void page_copy(void);

/* ---- 0x1c068: keep the page offsets, show page pal_d through 48000, tail jmp page_copy */
void fn_1c068(void)
{
    save_a = pal_a;
    save_b = pal_b;
    save_c = pal_c;
    pal_c = 48000;
    pal_a = pal_d;
    pal_b = pal_d;
    page_copy();
}

/* ---- 0x1c0ac: the reverse - pal_b = 48000, pal_a = pal_c = pal_d, page_copy, then restore */
void fn_1c0ac(void)
{
    pal_b = 48000;
    pal_a = pal_d;
    pal_c = pal_d;
    page_copy();
    pal_b = save_b;
    pal_c = save_c;
    pal_a = save_a;
}
