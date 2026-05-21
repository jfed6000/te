# te

te is a text editor for NitrOS-9 on 6809.  It uses a gap buffer and is currently limited to file < 16K.  te supports the following commands:

### FILE OPERATIONS:

^S              Save file

^Q              Quit (confirms if unsaved)
    
### EDITING:

^Z              Undo (50 levels)

Backspace       Delete character before cursor

Tab             Insert tab character

    
### SELECTION:

^A              Select all

Shift+Arrows    Select text

Shift+^Arrows   Select by word

ESC             Clear selection
    
### CLIPBOARD:

^C              Copy selection

^X              Cut selection

^V              Paste from clipboard
    
### SEARCH & NAVIGATION:

^F              Find text / Find next

^G              Go to line number

^Arrows         Move by word

^Up/Down        Page up/down
    
### DISPLAY:

^1              Single-spacing mode

^2              Double-spacing mode

### Compiling:

DCC (See CMOC branch if compiling with CMOC):

te is written in C, and compiled with dcc.  Compile with:

dcc te.c -m=2k

for Color Computer 3:  dcc te.c -m=2K -dcoco3

on some systems you may need to do this:

env -u LD_PRELOAD dcc te.c -m=2k

for Color Computer 3: env -u LD_PRELOAD dcc te.c -m=2k -dcoco3

NOTE: It won't work on the Color Computer 3 without a change to vtio.asm to buffer the kysns keys.

CMOC: To build with CMOC see the CMOC branch which has its own makefile and changes for CMOC.
