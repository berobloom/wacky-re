program T2;
uses Dos, Crt;
type Rec = array[0..15] of Byte;
var f: file of Rec; g: file; t: Text; r: Rec; s, s2: string; i, code: Integer;
    p: Pointer; regs: Registers; w: Word; l: LongInt; x: Real; c: Char;
begin
  Assign(f, 'A.DAT'); Reset(f); Read(f, r); Write(f, r); Seek(f, 1); Close(f);
  Assign(g, 'B.DAT'); Reset(g, 1); BlockRead(g, r, 16, w); BlockWrite(g, r, 16); Close(g);
  Rewrite(f); Erase(f);
  Assign(t, 'C.TXT'); Rewrite(t); Writeln(t, s, i, l); Close(t);
  Reset(t); Readln(t, s); Close(t);
  GetMem(p, 100); FreeMem(p, 100); New(p); w := MaxAvail; l := MemAvail;
  s := 'abc' + s; s2 := Copy(s, 2, 3); i := Pos('b', s); Delete(s, 1, 1); Insert('x', s, 1);
  if s = s2 then i := Length(s); if s < s2 then i := 2;
  s := c; c := UpCase(s[1]); Str(i, s); Str(l:5, s); Val(s, i, code); Val(s, l, code);
  i := Random(10); x := Random; Randomize; l := l * i; l := l div i; l := l mod i; l := l shl 2;
  x := x * 2.0 + i; i := Trunc(x); i := Round(x);
  Move(r, r[1], 3); FillChar(r, 16, 0); i := IOResult;
  Intr($10, regs); MsDos(regs); GetIntVec(9, p); SetIntVec(9, p); GetTime(w, w, w, w);
  Delay(10); if KeyPressed then c := ReadKey; ClrScr; GotoXY(1, 1); Sound(100); NoSound; TextColor(1);
  Writeln('x', i); Write(i:3); Readln(s); s := ParamStr(1); i := ParamCount;
  Halt(1);
end.
