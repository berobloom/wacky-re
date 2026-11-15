# captures/ MANIFEST

**Do not move, rename or delete a file named here: checks and tools read these
paths.** Anything not named here is one-shot evidence and may be purged;
regenerate it with the same call if needed.

Dumps and save states hold game code, so `.gitignore` keeps them local:
this file records how to regenerate them.

Record for each: what it is, how and when it was made (boot, segments or LE
deltas of that boot, the scene), and what reads it.

## Save states

| file | scene | boot (segments / deltas) | used by |
|---|---|---|---|

## Dumps

| file | linear range / segment:offset | taken when | used by |
|---|---|---|---|
