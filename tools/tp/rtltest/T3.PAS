program T3;
type Rec = array[0..41] of Byte;
var r: Rec; c: Char; b: Boolean; i: Integer;
procedure P(x: Rec);
begin
  i := x[3];
end;
begin
  P(r);
  c := Chr(i);
  b := c in ['a'..'z', '0', '_'];
  if b then Halt(1);
end.
