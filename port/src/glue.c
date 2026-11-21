/* glue.c: the C module's calls whose Watcom convention a plain alias cannot reproduce. */

void showlines_def(int n, int wait, int t);

/* `#pragma aux showlines value [ecx] parm [eax] [edx] [ebx] [ecx] modify []` (src/ww.c): the game's
 * callers take the result from ECX, which showlines_ (0x2a268) never changes, so it is the
 * caller's fourth argument (`ret = showlines(3, 1, 0, 1)`). */
int showlines(int n, int wait, int t, int ecx)
{
    showlines_def(n, wait, t);
    return ecx;
}
