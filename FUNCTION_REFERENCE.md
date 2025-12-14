# Text Editor Function Reference - v2046

**Version:** texteditMv2046.c  
**Date:** December 13, 2025  
**Lines:** 4168  
**Platform:** F256/OS-9, K&R C

---

## Table of Contents

1. [Core Data Structures](#core-data-structures)
2. [Gap Buffer Functions](#gap-buffer-functions)
3. [Movement Functions](#movement-functions)
4. [Helper Functions](#helper-functions)
5. [Display Functions](#display-functions)
6. [Selection Functions](#selection-functions)
7. [Clipboard Functions](#clipboard-functions)
8. [File Operations](#file-operations)
9. [Undo/Redo](#undoredo)
10. [Search Functions](#search-functions)

---

## Core Data Structures

### struct Buffer
Main data structure holding all editor state.

```c
struct Buffer {
    /* Text storage */
    char *text_storage;          /* Dynamically allocated text buffer */
    int gap_start;               /* Start of gap */
    int gap_end;                 /* End of gap */
    int text_length;             /* Actual text length (excluding gap) */
    int cursor_pos;              /* Cursor position in logical text */
    
    /* File info */
    char filename_storage[80];   /* Current filename */
    int dirty;                   /* Modified flag */
    
    /* Selection */
    int select_start;            /* Selection start position */
    int select_end;              /* Selection end position */
    int selecting;               /* Selection active flag */
    int selection_anchor;        /* Selection anchor point */
    
    /* Undo */
    struct UndoOp undo_buf[MAX_UNDO];  /* Circular undo buffer */
    int undo_count;              /* Number of undo operations */
    
    /* Caching */
    int ccurs_ln;                /* Current cursor line number */
    int topscr_pos;              /* Buffer position at top of screen */
    
    /* Search */
    char search_str[MAX_SEARCH]; /* Last search string */
    int search_pos;              /* Last search position */
    int search_active;           /* Search state */
};
```

**Global instance:** `struct Buffer buf`

---

## Gap Buffer Functions

### gap_char_at(pos)
**Purpose:** Get character at logical position (gap-aware)  
**Parameters:**
- `pos` - Logical position in text (0 to text_length-1)

**Returns:** Character at position

**Description:**  
Translates logical position to physical buffer position, accounting for the gap. Essential abstraction for v2 reuse.

```c
char gap_char_at(pos)
int pos;
{
    if (pos < buf.gap_start) {
        return *(text_ptr + pos);
    }
    return *(text_ptr + pos + (buf.gap_end - buf.gap_start));
}
```

**v2 Note:** Change this function to use piece table, everything else stays same!

---

### init_gap()
**Purpose:** Initialize empty gap buffer  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Sets up gap buffer with gap spanning entire buffer. Called at startup and after file load.

```c
init_gap()
{
    buf.gap_start = 0;
    buf.gap_end = BUF_SIZE;
    buf.text_length = 0;
}
```

---

### ensure_gap_at_cursor()
**Purpose:** Move gap to cursor position  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Moves the gap to current cursor position, preparing for insertion. Uses memmove to shift text.

**When called:**
- Before character insertion
- Before deletion

---

### gap_has_space()
**Purpose:** Check if gap has room for insertion  
**Parameters:** None  
**Returns:** 1 if space available, 0 if buffer full

**Description:**  
Simple check: `(buf.gap_end - buf.gap_start) > 0`

---

## Movement Functions

### move_up()
**Purpose:** Move cursor up one visual line  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Moves cursor to previous visual line, maintaining column position. Uses helper functions for clean implementation.

```c
move_up()
{
    int target, ch;
    int line_end;
    int at_line_end;
    
    /* Check if at line terminator */
    at_line_end = 0;
    if (buf.cursor_pos < buf.text_length) {
        ch = gap_char_at(buf.cursor_pos);
        if (IS_LINE_END(ch)) {
            at_line_end = 1;
        }
    }
    
    /* Get previous visual line */
    target = visln_pre(buf.cursor_pos);
    
    if (target >= buf.cursor_pos) return;  /* Can't go up */
    
    /* Find where this visual line ends */
    line_end = find_end(target);
    
    /* Position cursor at target column */
    target = pos_col(target, cursor_col, line_end);
    
    /* If at line end and target line shorter, go to its end */
    if (at_line_end && target >= line_end) {
        target = line_end;
    }
    
    set_curs(target);
    log_row--;
}
```

**Key features:**
- Maintains cursor column across lines
- Handles short lines intelligently
- Uses helper functions (find_end, pos_col)

---

### move_down()
**Purpose:** Move cursor down one visual line  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Mirror of move_up(), moves to next visual line. Nearly identical implementation.

---

### move_left()
**Purpose:** Move cursor left one position  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Simple wrapper around curs_lft(). Checks bounds.

```c
move_left()
{
    if (buf.cursor_pos > 0) {
        curs_lft();
    }
}
```

---

### move_right()
**Purpose:** Move cursor right one position  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Simple wrapper around curs_rgt(). Checks bounds.

---

### curs_lft()
**Purpose:** Move cursor left, updating screen coordinates  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Handles cursor left movement with proper column tracking. Handles tabs, newlines, and wrapping.

**Special cases:**
- Newline: Recalculate column
- Tab: Recalculate column (variable width)
- Regular char: Simple decrement
- Wrap: Adjust log_row

**Optimization:** Removed redundant calc_col() call (v2044)

---

### curs_rgt()
**Purpose:** Move cursor right, updating screen coordinates  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Handles cursor right movement with proper column tracking.

**Uses:** col_adv() helper for tab/char handling (v2045)

---

## Helper Functions

### col_adv(col, ch)
**Purpose:** Advance column by one character  
**Parameters:**
- `col` - Current column (0-79)
- `ch` - Character to advance over

**Returns:** New column position

**Description:**  
Core helper that handles tab vs. regular character advancement. Used throughout codebase for consistency.

```c
col_adv(col, ch)
int col;
char ch;
{
    if (ch == 9) {
        return NEXT_TAB(col);
    }
    return col + 1;
}
```

**Added in:** v2045  
**Uses:** 4+ locations  
**v2 Ready:** Yes - character-agnostic

---

### find_end(start)
**Purpose:** Find where visual line ends  
**Parameters:**
- `start` - Starting position of visual line

**Returns:** Position where visual line ends

**Description:**  
Scans forward from start, counting columns with tabs, until reaching:
- Line terminator (newline)
- Screen width (80 columns)
- Buffer end

```c
find_end(start)
int start;
{
    int pos, col;
    char ch;
    
    pos = start;
    col = 0;
    while (pos < buf.text_length) {
        ch = gap_char_at(pos);
        if (IS_LINE_END(ch)) break;
        col = col_adv(col, ch);
        if (col >= screen_cols) break;
        pos++;
    }
    return pos;
}
```

**Added in:** v2044  
**Used by:** move_up(), move_down()  
**v2 Ready:** Yes - uses gap_char_at() abstraction

---

### pos_col(start, tgt_col, line_end)
**Purpose:** Find position at target column on visual line  
**Parameters:**
- `start` - Beginning of visual line
- `tgt_col` - Target column to reach
- `line_end` - Don't go past this

**Returns:** Position at or near target column

**Description:**  
Scans forward from start, advancing columns until reaching target column or line end.

```c
pos_col(start, tgt_col, line_end)
int start, tgt_col, line_end;
{
    int pos, col;
    char ch;
    
    pos = start;
    col = 0;
    while (col < tgt_col && pos < line_end) {
        ch = gap_char_at(pos);
        col = col_adv(col, ch);
        if (col >= screen_cols) break;
        pos++;
    }
    return pos;
}
```

**Added in:** v2044  
**Used by:** move_up(), move_down()  
**v2 Ready:** Yes

---

### ensure_vis()
**Purpose:** Ensure cursor is visible on screen  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Checks if cursor is visible in current screen window. If not, scrolls to show it with context.

```c
ensure_vis()
{
    int screen_pos, vis_lines, screen_height, is_visible;
    int i, j;
    
    /* Check if cursor position is visible on screen */
    screen_pos = buf.topscr_pos;
    vis_lines = 0;
    screen_height = eff_rows;
    is_visible = 0;
    
    /* Walk through visible positions */
    while (screen_pos <= buf.cursor_pos && vis_lines < screen_height) {
        if (screen_pos == buf.cursor_pos) {
            is_visible = 1;
            break;
        }
        screen_pos = visln_next(screen_pos);
        vis_lines++;
        if (screen_pos <= buf.cursor_pos && screen_pos >= buf.text_length) break;
    }
    
    /* If off-screen, scroll to show it */
    if (!is_visible) {
        buf.topscr_pos = visln_sta(buf.cursor_pos);
        /* Add 5 lines of context */
        for (j = 0; j < 5 && buf.topscr_pos > 0; j++) {
            buf.topscr_pos = visln_pre(buf.topscr_pos);
        }
        need_full_redraw = 1;
    }
}
```

**Added in:** v2046  
**Used by:** Arrow keys with selection, could use elsewhere  
**v2 Ready:** Yes

---

### calc_col(pos)
**Purpose:** Calculate visual column at buffer position  
**Parameters:**
- `pos` - Buffer position

**Returns:** Visual column (0-79)

**Description:**  
Finds start of current visual line, then counts columns to position, handling tabs and wrapping.

**Uses:** col_adv() helper (v2045)

---

### set_curs(new_pos)
**Purpose:** Set cursor position and update line number  
**Parameters:**
- `new_pos` - New cursor position

**Returns:** Nothing

**Description:**  
Updates cursor position and incrementally updates line number cache. Core abstraction.

---

## Display Functions

### fast_scr()
**Purpose:** Redraw entire screen  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Draws title bar, text area, and status bar. Called on startup and major changes.

**Components:**
1. Title bar with filename and dirty flag
2. Text area (calls fast_show)
3. Status bar (calls draw_stat)
4. Help line

---

### fast_show()
**Purpose:** Draw visible text area  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Renders text from buf.topscr_pos for eff_rows lines. Handles:
- Soft wrapping
- Tab rendering
- Selection highlighting
- Double spacing mode

---

### draw_stat()
**Purpose:** Draw status bar  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Displays status message on left, position info on right. Uses caching to avoid flicker.

**Format:**
```
[status message]              L:line/total C:col KB
```

With selection:
```
[status message]        S:len L:line/total C:col KB
```

**Caching:** Only redraws if content changed (v2043+)

---

### fast_curs()
**Purpose:** Position cursor on screen  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Calculates physical screen position from logical position and moves cursor there.

---

## Selection Functions

### sel_active()
**Purpose:** Check if selection is active  
**Parameters:** None  
**Returns:** 1 if active, 0 otherwise

**Description:**  
Simple check: `buf.selecting && buf.select_start >= 0 && buf.select_end > buf.select_start`

---

### clr_sel()
**Purpose:** Clear selection  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Clears all selection state and triggers full redraw to remove highlights.

```c
clr_sel()
{
    if (buf.selecting) {
        buf.selecting = 0;
        buf.select_start = -1;
        buf.select_end = -1;
        buf.selection_anchor = -1;
        need_full_redraw = 1;
    }
}
```

---

### select_all()
**Purpose:** Select entire buffer (Ctrl+A)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Sets selection to span entire text. Updates status message.

---

### delete_selection()
**Purpose:** Delete selected text  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Removes selected text from buffer, updates cursor position, recounts lines.

---

## Clipboard Functions

### copy_sel()
**Purpose:** Copy selection to clipboard (Ctrl+C)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Copies up to 8K of selected text to internal clipboard. Shows status message.

---

### cut_sel()
**Purpose:** Cut selection to clipboard (Ctrl+X)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Copies selection then deletes it. Combination of copy_sel() and delete_selection().

---

### paste_cb()
**Purpose:** Paste from clipboard (Ctrl+V)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Inserts clipboard contents at cursor position. Creates undo state.

---

## File Operations

### load_file(filename)
**Purpose:** Load file into buffer  
**Parameters:**
- `filename` - Path to file

**Returns:** 0 on success, -1 on failure

**Description:**  
Reads file character by character into gap buffer. Updates state, recounts lines.

---

### save_file()
**Purpose:** Save buffer to current filename (Ctrl+S)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Writes buffer contents to file using gap-aware write. Clears dirty flag on success.

---

## Undo/Redo

### add_undo(pos, action, ch)
**Purpose:** Add operation to undo buffer  
**Parameters:**
- `pos` - Position of operation
- `action` - Type (INSERT or DELETE)
- `ch` - Character involved

**Returns:** Nothing

**Description:**  
Adds to circular undo buffer (50 operations). Oldest discarded when full.

---

### do_undo()
**Purpose:** Undo last operation (Ctrl+Z)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Reverses last operation:
- INSERT → delete character
- DELETE → insert character

---

## Search Functions

### find_next()
**Purpose:** Find next occurrence of search string  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Searches forward from cursor for search string. Wraps to beginning if needed.

**Features:**
- Case-insensitive
- Wrap-around search
- Status messages for not found

---

### find_first()
**Purpose:** Start new search (Ctrl+F)  
**Parameters:** None  
**Returns:** Nothing

**Description:**  
Prompts for search string, then calls find_next().

**Special:** If search string empty, uses last search (repeat find).

---

## Visual Line Functions

### visln_pre(pos)
**Purpose:** Find start of previous visual line  
**Parameters:**
- `pos` - Current position

**Returns:** Position of previous visual line start

**Description:**  
Scans backward handling wrapping. Complex but essential for navigation.

---

### visln_next(pos)
**Purpose:** Find start of next visual line  
**Parameters:**
- `pos` - Current position

**Returns:** Position of next visual line start

**Description:**  
Scans forward handling wrapping.

---

### visln_sta(pos)
**Purpose:** Find start of current visual line  
**Parameters:**
- `pos` - Position on line

**Returns:** Position of line start

**Description:**  
Scans backward to find where current visual line starts.

---

## Macros

### NEXT_TAB(col)
**Purpose:** Calculate next tab stop  
**Definition:** `((col/tab_wdth + 1) * tab_wdth)`

**Usage:** Convert column to next multiple of tab width

---

### IS_LINE_END(ch)
**Purpose:** Check if character is line terminator  
**Definition:** `(ch == '\r' || ch == '\n')`

**Usage:** Detect line boundaries

---

### PHYS_ROW(log_row)
**Purpose:** Convert logical row to physical screen row  
**Definition:** `((log_row) - scr_top + 2)`

**Usage:** Account for title bar when positioning

---

## Global Variables

### Display State
```c
int cursor_col;        /* Visual column (0-79) */
int log_row;           /* Logical row in display */
int scr_top;           /* Top logical row on screen */
int screen_cols;       /* Screen width (80) */
int eff_rows;          /* Effective rows for text */
```

### Flags
```c
int need_full_redraw;   /* Redraw entire screen */
int need_status_update; /* Redraw status bar */
int need_char_update;   /* Redraw character */
```

### Mode Flags
```c
int in_search_mode;    /* In search input */
int in_goto_mode;      /* In goto line input */
int quit_confirm;      /* Confirming quit */
int temp_message_active; /* Status clears on keystroke */
```

---

## Function Call Patterns

### Text Modification Pattern
```c
/* 1. Prepare */
ensure_gap_at_cursor();
if (!gap_has_space()) { /* handle full */ }

/* 2. Modify */
*(text_ptr + buf.gap_start) = ch;
buf.gap_start++;
buf.cursor_pos++;
buf.text_length++;

/* 3. Update state */
set_dirty(1);
add_undo(pos, INSERT, ch);

/* 4. Update display */
need_char_update = 1;
```

### Movement Pattern
```c
/* 1. Move cursor */
set_curs(new_pos);

/* 2. Update screen coords */
cursor_col = calc_col(new_pos);
log_row = /* calculated */;

/* 3. Ensure visible (if needed) */
ensure_vis();

/* 4. Update display */
need_status_update = 1;
```

### Selection Pattern
```c
/* 1. Check if active */
if (sel_active()) {
    /* handle selection */
}

/* 2. Position cursor */
set_curs(boundary);

/* 3. Clear */
clr_sel();

/* 4. Ensure visible */
ensure_vis();
```

---

## v2 Adaptation Guide

### Functions That Need Changes for v2

1. **gap_char_at()** - Change to piece table accessor
2. **Text modification** - Use piece table operations
3. **File I/O** - Different save/load logic

### Functions That Stay Unchanged

1. **All display functions** - Work with any text source
2. **All movement functions** - Use gap_char_at() abstraction
3. **All helper functions** - Character-agnostic
4. **Selection logic** - Position-based
5. **Undo structure** - May need different implementation

### Helper Functions Ready for v2

- col_adv() ✅
- find_end() ✅
- pos_col() ✅
- ensure_vis() ✅
- calc_col() ✅ (uses gap_char_at)
- move_up/down() ✅ (use helpers)

**Strategy:** Change gap_char_at(), rest follows!

---

## Performance Characteristics

### O(1) Operations
- gap_char_at() - Simple calculation
- col_adv() - Two comparisons
- set_curs() - Position update

### O(n) Operations (n = line length)
- find_end() - Scans one visual line
- pos_col() - Scans to column
- calc_col() - Scans from line start

### O(m) Operations (m = screen height)
- ensure_vis() - Walks visible lines
- fast_show() - Renders visible area

### O(k) Operations (k = selection size)
- copy_sel() - Copies selection
- delete_selection() - Moves text

**All reasonable for typical usage!**

---

## Code Quality Metrics

### K&R C Compliance
- ✅ Old-style function declarations
- ✅ No ANSI prototypes
- ✅ No struct assignment
- ✅ Manual string operations

### 8-Character Naming
- ✅ All identifiers unique in first 8 chars
- ✅ No conflicts (learned from v2040 bug!)

### Optimization Level
- ✅ Minimal duplication (Phases 1 & 2A)
- ✅ Helper functions extracted
- ✅ Smart caching where needed
- ✅ No premature optimization

---

*Complete function reference for texteditMv2046.c*
