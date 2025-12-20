/*
te - text editor for NItrOS-9 on F256 6809
(c) 2025 by John Federico
compile with dcc for NitrOS-9
dcc te.c -m=2k
on some systems you may need:
env -u LD_PRELOAD dcc te.c -m=2k
 */

#include <stdlib.h>
#include <stdio.h>
#include <sgstat.h>

struct sgbuf oldstat;

#define SS_OPT 0

/* Line terminator constants */
#define LF     10    /* $0A - Line Feed (Unix line ending) */
#define CR     13    /* $0D - Carriage Return (OS-9 line ending) */

/* Macro to check if character is a line terminator */
#define IS_LINE_END(ch) ((ch) == LF || (ch) == CR)

/* Tab width configuration (8-char limit: tab_wdth) */
/* Use NEXT_TAB(col) to get next tab stop, TAB_MOD(col) for column offset */
#define NEXT_TAB(col) (((col) / tab_wdth + 1) * tab_wdth)
#define TAB_MOD(col)  ((col) % tab_wdth)

/* Convert logical text row to physical display row for double-spacing */
#define PHYS_ROW(lr) (text_start_row + ((lr) * (dbl_space ? 2 : 1)))

/* Call this at editor startup - DISABLE ECHO completely */
set_raw_mode()
{
    /* Get current settings for stdin (path 0) */
    getstat(SS_OPT, 0, &oldstat);


    oldstat.sg_echo = 0;
    oldstat.sg_kbich = 0;
    oldstat.sg_kbach = 0;
    
    /* Apply settings to stdin (path 0) */
    setstat(SS_OPT, 0, &oldstat);
}

/* Call this at editor exit */
restore_mode()
{
    /* Restore original settings */

    oldstat.sg_echo = 1;
    oldstat.sg_kbich = 3;
    oldstat.sg_kbach = 5;
    
    setstat(SS_OPT, 0, &oldstat);
}

int get_kysns(path)
int path;
{
#asm
 lda 5,s        * Get path parameter from stack
 ldb #$27       * SS.KySns code ($27)
 os9 $8D        * I$GetStt system call
 bcc _kysns_ok  * Branch if no error
 ldd #0         * Return 0 on error
 bra _kysns_end
_kysns_ok:
 tfr a,b        * Move KySns result from A to B  
 clra           * Clear A (high byte)
_kysns_end:
#endasm
}


int kbhit(path)
int path;
{
#asm
 lda 5,s        * Get path parameter from stack
 ldb #$01      * SS.Ready code ($01)
 os9 $8D        * I$GetStt system call
 bcc _ready_ok  * Branch if no error
 ldd #0         * Return 0 on error (no data ready)
 bra _ready_end
_ready_ok:
 ldd #1         * Return 1 on success (data ready)
_ready_end:
#endasm
}

int get_cols()
{
#asm
 pshs y         * Save Y register (critical!)
 lda #0         * Path 0 (stdin)
 ldb #$26       * SS.ScSiz code ($26)
 os9 $8D        * I$GetStt system call
 bcc _cols_ok   * Branch if no error
 ldd #80        * Return default 80 on error
 bra _cols_end
_cols_ok:
 tfr x,d        * Move columns from X to D (safe return register)
_cols_end:
 puls y         * Restore Y register
#endasm
}

int get_rows()
{
#asm
 pshs y         * Save Y register  
 lda #0         * Path 0 (stdin)
 ldb #$26       * SS.ScSiz code ($26)
 os9 $8D        * I$GetStt system call
 bcc _rows_ok   * Branch if no error
 ldd #60        * Return default 60 on error
 bra _r_end
_rows_ok:
 tfr y,d        * Move rows from Y to D, then restore Y
 puls y         * Restore Y register 
 bra _r_end2
_r_end:
 puls y         * Restore Y register on error path too
_r_end2:
#endasm
}

/* OS-9 immediate keyboard input with F256 arrow key support */
int inkey()
{
    char ch;
    int key_status;
    if (kbhit(0)){
	if (read(0, &ch, 1) > 0) {
	  /* Get keyboard status for stdin (path 0) */
	  key_status = get_kysns(0);
	  return (key_status << 8) | (ch & 0xFF);
	}
      }
    return 0;
}

int write_block(path, buffer, len)
int path, len;
char *buffer;
{
#asm
    * Save Y register (compiler uses it for data area base)
    pshs y
    
    * Get parameters from stack - write_block(path, buffer, len)
    ldy 10,s        * Get len parameter into Y register (offset +2 due to pshs)
    beq _write_zero * If length is 0, return 0
    lda 6,s        * Get path parameter into A register (offset +2 due to pshs)
    ldx 8,s        * Get buffer pointer into X register (offset +2 due to pshs)
    
    * OS-9 I$Write system call
    os9 $8C        * I$Write system call
    
    * Check for error
    bcc _write_ok  * Branch if no carry (no error)
    
    * Error occurred - return -1
    puls y         * Restore Y register
    ldd #$ffff     * Return -1 on error
    bra _write_end
    
_write_ok:
    * Success - return number of bytes written (Y register has bytes written)
    tfr y,d        * Move bytes written from Y to D (return value)
    puls y         * Restore Y register
    bra _write_end
    
_write_zero:
    * Zero length request - return 0
    puls y         * Restore Y register
    ldd #0
    
_write_end:
#endasm
}



/* Key definitions with context-aware handling */

#define KEY_C_C         3    /* Ctrl+C for COPY (no longer force quit) */
#define KEY_C_S         19
#define KEY_C_F         6    /* Ctrl+F for find */
#define KEY_C_G         7    /* Ctrl+G for goto line */
#define KEY_C_H         8    /* Ctrl+H for help */
#define KEY_C_A         1    /* Ctrl+A for select all */
#define KEY_C_Q         17   /* Ctrl+Q for quit */
#define KEY_C_V         22   /* Ctrl+V for PASTE */
#define KEY_C_X         24   /* Ctrl+X for CUT */
#define KEY_C_Z         26   /* Ctrl+Z for UNDO */
#define KEY_C_Y         25   /* Ctrl+Y for REDO */
#define KEY_ENTER       13
#define KEY_TAB         9

#ifdef  coco3
#define KEY_ESC         177
#define KEY_C_1         181   /* F1 for single-spacing */
#define KEY_C_2         182   /* F2 for double-spacing */
/* F256 arrow key codes */
#define KEY_UP          12
#define KEY_DOWN        10
#define KEY_LEFT        8
#define KEY_RIGHT       9
#define KEY_S_UP        28
#define KEY_S_DOWN      26
#define KEY_S_LEFT      24
#define KEY_S_RIGHT     25
#define KEY_C_UP        19
#define KEY_C_DOWN      18
#define KEY_C_LEFT      16
#define KEY_C_RIGHT     17
#define KEY_BS          5

#else
#define KEY_ESC         5
#define KEY_C_1         49   /* Ctrl+1 for single-spacing */
#define KEY_C_2         50   /* Ctrl+2 for double-spacing */
/* F256 arrow key codes */
#define KEY_UP          12
#define KEY_DOWN        10
#define KEY_LEFT        8
#define KEY_RIGHT       9
#define KEY_S_UP        12
#define KEY_S_DOWN      10
#define KEY_S_LEFT      8
#define KEY_S_RIGHT     9
#define KEY_C_UP        12
#define KEY_C_DOWN      10
#define KEY_C_LEFT      8
#define KEY_C_RIGHT     9
#define KEY_BS          8
#endif

/* Buffer and screen constants */
#define BUF_SIZE        16384
#define MAX_UNDO        50
#define MAX_SEARCH      32    /* Search string length */

/* F256 keyboard status bits (KySns register) */
#define SHIFT_BIT       0x01    /* Bit 0 = Shift */
#define CTRL_BIT        0x02    /* Bit 1 = CTRL */  
#define ALT_BIT         0x04    /* Bit 2 = ALT */
#define UPBIT           0x08    /* Bit 3 = Up Arrow */
#define DOWNBIT         0x10    /* Bit 4 = Down Arrow */
#define LEFTBIT         0x20    /* Bit 5 = Left Arrow */
#define RIGHTBIT        0x40    /* Bit 6 = Right Arrow */
#define SPACE_BIT       0x80    /* Bit 7 = Spacebar */

/* Graphics character definitions - 8 bytes each */
unsigned char topleftbc[8] = {0x00,0x00,0x00,0x1F,0x18,0x1F,0x18,0x18};
unsigned char toprigtbc[8] = {0x00,0x00,0x00,0xF8,0x18,0xF8,0x18,0x18};
unsigned char topltc[8]    = {0x18,0x18,0x18,0xF8,0x18,0xF8,0x18,0x18};
unsigned char toprtc[8]    = {0x18,0x18,0x18,0x1F,0x18,0x1F,0x18,0x18};
unsigned char topbl[8]     = {0x00,0x00,0x00,0xFF,0x00,0xFF,0x00,0x00};
unsigned char sidebl[8]    = {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18};
unsigned char btmlftbc[8]  = {0x18,0x18,0x18,0x1F,0x18,0x1F,0x00,0x00};
unsigned char btmrgtbc[8]  = {0x18,0x18,0x18,0xF8,0x18,0xF8,0x00,0x00};
unsigned char sldchar[8]   = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
unsigned char uparr[8]     = {0x20,0x30,0x38,0x3C,0x38,0x30,0x20,0x00};
unsigned char dnarr[8]     = {0x04,0x0C,0x1C,0x3C,0x1C,0x0C,0x04,0x00};
unsigned char lftarr[8]    = {0x00,0x00,0x10,0x38,0x7C,0xFE,0x00,0x00};
unsigned char rgtarr[8]    = {0x00,0x00,0x00,0xFE,0x7C,0x38,0x10,0x00};

/* Storage for old characters - 13 chars * 8 bytes each */
unsigned char oldchars[104];  /* 13*8 = 104 */

/* Pointer to all new chars in order */
unsigned char *newchars[13];

/* Initialize the character array pointers */
init_chars()
{
    newchars[0] = topleftbc;
    newchars[1] = toprigtbc;
    newchars[2] = topltc;
    newchars[3] = toprtc;
    newchars[4] = topbl;
    newchars[5] = sidebl;
    newchars[6] = btmlftbc;
    newchars[7] = btmrgtbc;
    newchars[8] = sldchar;
    newchars[9] = uparr;
    newchars[10] = dnarr;
    newchars[11] = lftarr;
    newchars[12] = rgtarr;
}

/* Get font character from OS-9 */
int getfntch(charnum, buffer)
int charnum;
char *buffer;
{
#asm
    pshs y
    ldy  6,s      * Get charnum into Y
    ldx  8,s      * Get buffer pointer into X  
    lda  #0       * Path 0 (stdin)
    ldb  #$C2     * SS.FntChar ($19)
    os9  $8D      * I$GetStt
    bcc  _gfc_ok
    ldd  #-1      * Return -1 on error
    puls y
    bra  _gfc_end
_gfc_ok:
    ldd  #0       * Return 0 on success
    puls y
_gfc_end:
#endasm
}

/* Set font character in OS-9 */
int setfntch(charnum, buffer)
int charnum;
char *buffer;
{
#asm
    pshs y
    ldy  6,s      * Get charnum into Y
    ldx  8,s      * Get buffer pointer into X
    lda  #0       * Path 0 (stdin)
    ldb  #$C2     * SS.FntChar ($19)
    os9  $8E      * I$SetStt
    bcc  _sfc_ok
    ldd  #-1      * Return -1 on error
    puls y
    bra  _sfc_end
_sfc_ok:
    ldd  #0       * Return 0 on success
    puls y
_sfc_end:
#endasm
}

/* Install custom characters */
inst_chr()
{
    int i, j;
    int charnum;
    char *oldptr;
    
    init_chars();
    
    /* Save old characters and install new ones */
    charnum = 242;
    oldptr = oldchars;
    
    for (i = 0; i < 13; i++) {
        /* Get old character */
        if (getfntch(charnum, oldptr) < 0) {
            return -1;
        }
        
        /* Set new character */
        if (setfntch(charnum, newchars[i]) < 0) {
            return -1;
        }
        
        charnum++;
        oldptr += 8;
    }
    
    return 0;
}

/* Restore original characters */
rest_chr()
{
    int i;
    int charnum;
    char *oldptr;
    
    charnum = 242;
    oldptr = oldchars;
    
    for (i = 0; i < 13; i++) {
        setfntch(charnum, oldptr);
        charnum++;
        oldptr += 8;
    }
}

/* Character codes for use in drawing */
#ifdef coco3
#define CH_TOPLTC  124
#define CH_TOPRTC  124
#define CH_TOPBL   45
#else
#define CH_TOPLTC  244
#define CH_TOPRTC  245
#define CH_TOPBL   246
#endif

/* Undo structure */
struct UndoEntry {
    int pos;
    int action;
    char ch;
};

struct Clipboard {
    int start_block;       /* Block number from F$AllRAM */
    char *mapped_addr;     /* Mapped address from F$MapBlk */
    int data_length;       /* Current clipboard content length */
    int max_capacity;      /* 8K capacity */
    int initialized;       /* Successfully allocated? */
} clipboard;


char status_storage[80];
char *status_msg;
int in_search_mode;      /* Search mode - typing search string */
int in_find_mode;        /* Find mode - after finding, ready to find next */
int in_goto_mode;        /* Goto line mode - typing line number */
int in_help_mode;        /* Help screen mode - displaying help overlay */
char temp_search_str[MAX_SEARCH];  /* Current search session input */
char goto_line_str[8];   /* Line number input (max 9999999) */
int screen_rows;
int screen_cols;
int tab_wdth;            /* Configurable tab width (default 8) */

/* Screen tracking */
int log_row;         /* Logical text row */
int cursor_col;
int cur_end_col;
int text_start_row;
int status_row;
int dbl_space;       /* Double-spacing display flag */
int eff_rows;        /* Effective text rows (accounting for double-spacing) */

/* Pre-built control sequences for write_block */
char HIDE_CURSOR[2];
char SHOW_CURSOR[2];
char REV_ON[2];
char REV_OFF[2];
char CLEAR_EOL[1];
char ERASE_LINE[1];
char CLEAR_SCREEN[1];
char HOME_CURSOR[1];
char POS_BUF[3];  /* For XY positioning sequences */

/* Screen optimization flags */
int need_char_update;
int need_full_redraw;
int need_status_update;
int need_title_update;
int last_cursor_pos;
int need_minimal_update;
int need_redraw_down;
int update_from_pos;

/* Wrap detection globals */
int total_logical_lines;  /* total logical lines in buffer - tracked incrementally */
int temp_message_active;  /* 1 = status message clears on next keystroke */

/* File pointers */
char *fname_ptr;
char *text_ptr;

int  quit_confirm;  /* global for quitting with a dirty buffer */ 

/* Enhanced buffer structure with selection and search */
struct Buffer {
    char filename_storage[32];


    int gap_start;
    int gap_end;
    
    int text_length;
    int cursor_pos;
    int dirty;
    /* Enhanced selection support */
    int select_start;
    int select_end;
    int selecting;
    int selection_anchor;    /* Where selection started */
    /* Undo system */
    struct UndoEntry undo_buf[MAX_UNDO];
    int undo_count;
    /* Enhanced caching */
    int ccurs_ln;          /* Current cursor line number */
    int topscr_pos;              /* Buffer position at top of screen */
    /* Search functionality */
    char search_str[MAX_SEARCH];
    int search_pos;          /* Last found position */
    int search_active;       /* Search mode active */
    char *text_storage;
};

/* Global variables */
struct Buffer buf;

/* Function declarations with soft wrap support */
main();
init_ed();
load_file();
save_file();
draw_stat();
main_loop();
col_adv();
find_end();
pos_col();
move_up();
move_down();
move_left();
move_right();
/* Cursor update helpers */
calc_col();
curs_lft();
curs_rgt();
/* Enhanced navigation */
word_left();
word_right();
page_up();
page_down();
page_curs();
/* Selection functions */
ex_left();
ex_right();
ex_up();
ex_down();
ex_wd_left();
ex_wd_right();
sel_all();
copy_sel();
del_sel();
/* Search functions */
strtsch();
find_next();
find_prev();
end_search();
start_goto();
end_goto();
goto_keys();
/* Existing functions */
edit_key();
do_char();
do_back();
do_cr_enter();
do_lf_enter();
add_undo();
do_undo();
get_line();
line_sta();
line_end();
ensure_vis();
goto_ln();
/* Update functions */
update_scr();
vis_col();
draw_title();
set_dirty();
clear_eol();
/* Word movement utilities */
is_word_char();
fdwd_start();
fdwd_end();
/* Selection utilities */
start_sel();
clr_sel();
sel_active();
search_keys();
/* Gap Buffer Functions */
init_gap();
init_line_cache();
char gap_char_at();
move_gap_to();
ensure_gap_at_cursor();
gap_has_space();
gap_size();
set_curs();
/* Fast display routines */
fast_show();
fast_curs();
fast_scr();
fast_upd();
fast_char_upd();
upd_fast();
fast_from_pos();
fast_line();
init_caches();
get_top_pos();
visln_pre();
visln_next();
visln_sta();
redraw_char_at_screen_pos();
write_pos();
calc_end_col();

/* Main program */
main(argc, argv)
int argc;
char *argv[];
{
    status_msg = status_storage;
    fname_ptr = buf.filename_storage;
    
    init_ed();
    
    text_ptr = buf.text_storage;

    fast_scr();
    
    if (argc > 1) {
        strcpy(fname_ptr, argv[1]);      /* Use provided filename */
        if (load_file(argv[1]) == -1) {  /* File doesn't exist */
            set_dirty(1);                 /* Mark as new/unsaved file */
        }
    }
    

    main_loop();
    
    return 0;
}



/* Initialize gap buffer */
init_gap()
{
    buf.gap_start = 0;
    buf.gap_end = BUF_SIZE;          /* Entire buffer is gap initially */
    buf.text_length = 0;
}

init_caches()
{
    buf.ccurs_ln = 0;
    buf.topscr_pos = 0;

}



/* Gap-aware write function */


write_gap_block(path, start_pos, len)
int path, start_pos, len;
{
    int end_pos;
    int written;
    int gap_st, gap_end, text_len;
    int aft_st, aft_len;
    int phys_st;
    
    /* Copy struct members to local variables to avoid compiler issues */
    gap_st = buf.gap_start;
    gap_end = buf.gap_end;
    text_len = buf.text_length;
    
    end_pos = start_pos + len;
    written = 0;
    
    /* Validate bounds */
    if (start_pos < 0 || start_pos >= text_len) return 0;
    if (end_pos > text_len) end_pos = text_len;
    
    if (start_pos < gap_st) {
        if (end_pos <= gap_st) {
            /* Entire chunk is before gap - single write */
            return write_block(path, text_ptr + start_pos, end_pos - start_pos);
        } else {
            /* Chunk spans gap - write two parts */
            written += write_block(path, text_ptr + start_pos, gap_st - start_pos);
            aft_st = gap_end;
            aft_len = end_pos - gap_st;
            written += write_block(path, text_ptr + aft_st, aft_len);
            return written;
        }
    } else {
        /* Entire chunk is after gap */
        phys_st = start_pos + (gap_end - gap_st);
        return write_block(path, text_ptr + phys_st, end_pos - start_pos);
    }
}

redraw_char_at_screen_pos(col, row, buffer_pos)
int col, row, buffer_pos;
{
    char ch;
    int was_reversed;
    was_reversed=0;
    /* Position cursor at screen location */
    write_pos(col, row);
    
    /* Set correct reverse video state */
    if (buf.selecting && buffer_pos >= buf.select_start && buffer_pos < buf.select_end) {
      write_block(1,REV_ON,2);  /* Reverse on */
      was_reversed=1;
    }     
    /* Redraw the character */
    if (buffer_pos < buf.text_length) {
        ch = gap_char_at(buffer_pos);
        if (ch >= 32 && ch < 127) {
            putchar(ch);
        } else if (ch == 9) {
            /* Handle tab - might need special logic */
            putchar(' ');  /* Simplified */
        }
    }

    if(was_reversed){
      write_block(1,REV_OFF,2);
    }

    /* Restore cursor position */
    write_pos(cursor_col, PHYS_ROW(log_row));
}

/* Build XY position sequence and write it */
write_pos(col, row)
int col, row;
{
    POS_BUF[0] = 0x02;
    POS_BUF[1] = col + 0x20;
    POS_BUF[2] = row + 0x20;
    write_block(1, POS_BUF, 3);
}

/* Get top screen position - always valid in buffer position system */
get_top_pos()
{
    return buf.topscr_pos;
}

/* Get character at logical position (gap-aware) */
char gap_char_at(pos)
int pos;
{
    if (pos < 0 || pos >= buf.text_length) {
        return 0;  /* Out of bounds */
    }
    
    if (pos < buf.gap_start) {
        /* Character is before the gap */
        return *(text_ptr + pos);
    } else {
        /* Character is after the gap - skip over gap */
        return *(text_ptr + pos + (buf.gap_end - buf.gap_start));
    }
}

/* Move gap to specified position */
move_gap_to(target_pos)
int target_pos;
{
    int gap_size, move_count, i;
    int og_start, og_end;
    og_start = buf.gap_start;
    og_end = buf.gap_end;
    
    /* Validate target position */
    if (target_pos < 0) target_pos = 0;
    if (target_pos > buf.text_length) target_pos = buf.text_length;
    
    /* Already at target position? */
    if (target_pos == buf.gap_start) return;

    gap_size = buf.gap_end - buf.gap_start;
    
    if (target_pos < buf.gap_start) {
        /* Move gap left - copy text from before gap to after gap */
        move_count = buf.gap_start - target_pos;
        
        /* Move text: [target_pos...gap_start) -> [gap_end-move_count...gap_end) */
        for (i = 0; i < move_count; i++) {
            *(text_ptr + buf.gap_end - 1 - i) = *(text_ptr + buf.gap_start - 1 - i);
        }
        
        /* Update gap boundaries */
        buf.gap_start = target_pos;
        buf.gap_end = target_pos + gap_size;
        
    } else {
        /* Move gap right - copy text from after gap to before gap */
        move_count = target_pos - buf.gap_start;
        
        /* Move text: [gap_end...gap_end+move_count) -> [gap_start...gap_start+move_count) */
        for (i = 0; i < move_count; i++) {
            *(text_ptr + buf.gap_start + i) = *(text_ptr + buf.gap_end + i);
        }
        
        /* Update gap boundaries */
        buf.gap_start = target_pos;
        buf.gap_end = target_pos + gap_size;
    }
}

/* Ensure gap is at cursor position (for efficient editing) */
ensure_gap_at_cursor()
{
    move_gap_to(buf.cursor_pos);
}

/* Check if gap has space for insertion */
gap_has_space()
{
    return (buf.gap_start < buf.gap_end);
}

/* Get current gap size */
gap_size()
{
    return buf.gap_end - buf.gap_start;
}

/* Add these functions after the existing gap buffer functions, around line 500 */

/* Allocate RAM blocks - returns starting block number */
int alloc_ram_blocks(num_blocks)
int num_blocks;
{
#asm
    ldb 5,s          * Get num_blocks from stack into B
    os9 $39          * F$AllRAM  Allocate RAM blocks
    bcs _alloc_fail  * Branch if error
    * D register contains starting block number
    bra _alloc_done
_alloc_fail:
    ldd #-1          * Return -1 on failure
_alloc_done:
#endasm
}

/* Map blocks into address space */
char *map_blocks(start_block, num_blocks)
int start_block, num_blocks;
{
#asm
    ldx 4,s          * Get start_block into X
    ldb 7,s          * Get num_blocks into B  
    os9 $4F          * Map blocks F$MapBlk
    bcs _map_fail    * Branch if error
    tfr u,d          * Return address from U register
    bra _map_done
_map_fail:
    ldd #0           * Return NULL on failure
_map_done:
#endasm
}

/* Unmap blocks */
unmap_blocks(addr, num_blocks)
char *addr;
int num_blocks;
{
#asm
    ldu 4,s          * Get address into U
    ldb 7,s          * Get num_blocks into B
    os9 $50          * Unmap blocks F$ClrBlk  
#endasm
}

/* Free RAM blocks */
free_ram_blocks(start_block, num_blocks)
int start_block, num_blocks;
{
#asm
    ldx 4,s          * Get start_block into X
    ldb 7,s          * Get num_blocks into B
    os9 $51          * Free RAM blocks F$DelRAM  
#endasm
}

/* Add this function after the system call wrappers */
init_clipboard()
{
    clipboard.initialized = 0;
    clipboard.data_length = 0;
    clipboard.max_capacity = 8192;  /* 8K capacity */
    
    /* Allocate exactly 1 block (8K) */
    clipboard.start_block = alloc_ram_blocks(1);
    if (clipboard.start_block < 0) {
        strcpy(status_msg, "Warning: No clipboard (no RAM available)");
        return -1;
    }
    
    /* Map the block permanently */
    clipboard.mapped_addr = map_blocks(clipboard.start_block, 1);
    if (clipboard.mapped_addr == NULL) {
        free_ram_blocks(clipboard.start_block, 1);
        strcpy(status_msg, "Warning: No clipboard (map failed)");
        return -1;
    }
    
    clipboard.initialized = 1;
    strcpy(status_msg, "8K clipboard initialized");
    return 0;
}

/* Add this function after init_clipboard() */
copy_gap_selection_to_clipboard(sel_len)
int sel_len;
{
    if (buf.select_start < buf.gap_start) {
        if (buf.select_end <= buf.gap_start) {
            /* Simple case: all before gap */
            memcpy(clipboard.mapped_addr, text_ptr + buf.select_start, sel_len);
        } else {
            /* Complex case: spans gap */
            int first_part = buf.gap_start - buf.select_start;
            int second_part = sel_len - first_part;
            
            /* Copy first part (before gap) */
            memcpy(clipboard.mapped_addr, text_ptr + buf.select_start, first_part);
            
            /* Copy second part (after gap) */
            memcpy(clipboard.mapped_addr + first_part, 
                   text_ptr + buf.gap_end, second_part);
        }
    } else {
        /* Simple case: all after gap */
        int physical_pos = buf.select_start + (buf.gap_end - buf.gap_start);
        memcpy(clipboard.mapped_addr, text_ptr + physical_pos, sel_len);
    }
}

cleanup_clipboard()
{
    if (clipboard.initialized && clipboard.mapped_addr != NULL) {
        unmap_blocks(clipboard.mapped_addr, 1);
        free_ram_blocks(clipboard.start_block, 1);
        clipboard.initialized = 0;
    }
}

/* Initialize enhanced editor */
init_ed()
{
    int i;
    
    /* Clear buffer structure */
    for (i = 0; i < sizeof(struct Buffer); i++) {
        ((char*)&buf)[i] = 0;
    }

    buf.text_storage = malloc(BUF_SIZE);
    if (buf.text_storage == NULL) {
        printf("Fatal: Cannot allocate text buffer\n");
        exit(1);
    }
#ifndef coco3
    inst_chr();
#endif    
    /* Initialize gap buffer */
    init_gap();
    
    /* Initialize all caches */
    init_caches();
    
    /* Rest of initialization... */
    buf.cursor_pos = 0;
    buf.dirty = 0;
    buf.select_start = -1;
    buf.select_end = -1;
    buf.selecting = 0;
    buf.selection_anchor = -1;
    buf.undo_count = 0;
    buf.search_pos = -1;
    buf.search_active = 0;

    /* Initialize control sequences in init_ed() */
    HIDE_CURSOR[0] = 0x05;
    HIDE_CURSOR[1] = 0x20;

    REV_ON[0] = 0x1F;
    REV_ON[1] = 0x20;
    REV_OFF[0] = 0x1F;
    REV_OFF[1] = 0x21;
    
    SHOW_CURSOR[0] = 0x05;
    SHOW_CURSOR[1] = 0x21;

    ERASE_LINE[0] = 0x03;
    CLEAR_EOL[0] = 0x04;
    CLEAR_SCREEN[0] = 0x0C;
    HOME_CURSOR[0] = 0x01;
    
    strcpy(fname_ptr, "untitled.txt");
    strcpy(status_msg, "Fast Editor v2.0 - Gap Buffer + Caching");

    init_clipboard();
    
    in_search_mode = 0;
    in_find_mode = 0;
    in_goto_mode = 0;
    in_help_mode = 0;
    quit_confirm = 0;
    temp_message_active = 0;
    total_logical_lines = 1;  /* Will be recounted on file load */

    //   detect_screen_dimentions();
    screen_cols = get_cols();
    screen_rows = get_rows();
    tab_wdth = 8;          /* Default tab width to 8 columns */
    dbl_space = 0;         /* Default to single-spacing */
    log_row = 0;           /* Start at first logical text row */
    cursor_col = 0;
    text_start_row = 1;
    status_row = screen_rows - 3;
    
    /* Calculate effective text rows based on double-spacing */
    if (dbl_space) {
        eff_rows = (status_row - text_start_row) / 2;
    } else {
        eff_rows = status_row - text_start_row;
    }
    
    /* Screen optimization */
    need_full_redraw = 1;
    need_status_update = 0;
    need_title_update = 0;
    need_minimal_update = 0;
    need_char_update = 0;
    need_redraw_down = 0;
    update_from_pos = -1;
    last_cursor_pos = 0;
    
    putchar(0x0C);  /* Clear screen */
    set_raw_mode();
}

/* Count total logical lines in buffer - called after major changes */
recount_total_lines()
{
    int i;
    int line_count;
    
    line_count = 1;  /* Start with line 1 */
    
    for (i = 0; i < buf.text_length; i++) {
        if (IS_LINE_END(gap_char_at(i))) {
            line_count++;
        }
    }
    
    total_logical_lines = line_count;
}

/* Set a temporary status message that clears on next keystroke */
set_temp_status(msg)
char *msg;
{
    strcpy(status_msg, msg);
    temp_message_active = 1;
    need_status_update = 1;  /* Ensure message gets displayed */
}

/* File operations - unchanged from working version */
load_file(filename)
char *filename;
{
    FILE *fp;
    int ch, count;
    
    fp = fopen(filename, "r");
    if (fp == 0) {
        strcpy(status_msg, "Could not open file");
        return -1;
    }
    
    /* Initialize gap buffer - gap at position 0, ready for insertion */
    init_gap();
    
    count = 0;
    
    /* Load characters using gap buffer insertion at cursor position */
    while ((ch = fgetc(fp)) != EOF && buf.text_length < BUF_SIZE - 1) {
        /* Ensure gap is at current insertion point (cursor position) */
        ensure_gap_at_cursor();
        
        if (!gap_has_space()) {
            break;  /* Buffer full */
        }
        
        /* Insert character into gap */
        *(text_ptr + buf.gap_start) = ch;
        buf.gap_start++;
        buf.cursor_pos++;
        buf.text_length++;
        count++;
        
        if (count % 1000 == 0) {
            printf("Loading... %d bytes\r", count);
        }
    }
    
    fclose(fp);
    
    /* Set cursor to beginning of file */
    buf.cursor_pos = 0;
    ensure_gap_at_cursor();
     
    /* Update filename and clear dirty flag */
    strcpy(fname_ptr, filename);
    buf.dirty = 0;
    buf.undo_count = 0;
    
    /* Initialize line cache after loading */
    recount_total_lines();  /* Count total lines in file */
    
/* Force full screen redraw to show new filename and content */
    buf.topscr_pos = 0;
    set_curs(0);
    log_row = 0;  /* First logical row */
    cursor_col = 0;
    need_full_redraw = 1;
    
    /* Position cursor at same relative location */
    fast_curs();
    need_full_redraw = 1;
    
    return 0;
}

save_file()
{
    FILE *fp;
    int i;
    
    fp = fopen(fname_ptr, "w");
    if (fp == 0) {
        strcpy(status_msg, "Could not save file");
        return -1;
    }
    
    /* Write text before gap */
    for (i = 0; i < buf.gap_start; i++) {
        fputc(*(text_ptr + i), fp);
    }
    
    /* Write text after gap */
    for (i = buf.gap_end; i < BUF_SIZE; i++) {
        /* Only write if this position contains valid text */
        if (i - buf.gap_end < buf.text_length - buf.gap_start) {
            fputc(*(text_ptr + i), fp);
        } else {
            break;  /* No more valid text */
        }
    }
    
    fclose(fp);
    set_dirty(0);
    sprintf(status_msg, "Saved %d bytes", buf.text_length);
    temp_message_active = 1;
    return 0;
}

/* ENHANCED MAIN LOOP with advanced key combinations */
main_loop()
{
    int key, key_status, key_char;
    
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    
    need_full_redraw = 1;
    upd_fast(); 
    
    while (1) {
        key = inkey();
        
        if (key != 0) {
            key_status = (key >> 8) & 0xFF;
            key_char = key & 0xFF;

	    /* Handle help mode first - any key exits */
	    if (in_help_mode) {
	        hide_help();
	        continue;
	    }

	    /* Handle search mode first - before other key processing */
	    if (in_search_mode) {
	      /* Special handling for Ctrl+F in search mode */
	      if ((key_status & CTRL_BIT) && key_char == KEY_C_F) {
	        /* Execute first find (from search mode) */
	        if (temp_search_str[0] != 0) {
	          /* User typed something - use it */
	          strcpy(buf.search_str, temp_search_str);
	          find_next();
	          in_search_mode = 0;
	          in_find_mode = 1;
	        } else if (buf.search_str[0] != 0) {
	          /* Blank but previous search exists - reuse it */
	          find_next();
	          in_search_mode = 0;
	          in_find_mode = 1;
	        }
	        /* Else: blank and no previous search - do nothing, stay in search mode */
	        need_status_update = 1;
	        upd_fast();
	        continue;
	      }
	      
	      /* Regular search mode key handling */
	      search_keys(key_char);
	      need_status_update = 1;
	      upd_fast();
	      continue;  /* Skip other key processing */
	    }
	
	    /* Handle goto line mode */
	    if (in_goto_mode) {
	      goto_keys(key_char);
	      need_status_update = 1;
	      upd_fast();
	      continue;  /* Skip other key processing */
	    }
	
	    /* Exit find mode if any key other than Ctrl+F is pressed */
	    if (in_find_mode) {
	      if (!((key_status & CTRL_BIT) && key_char == KEY_C_F)) {
	        /* User pressed something other than Ctrl+F - exit find mode */
	        in_find_mode = 0;
	        strcpy(status_msg, "");
	        /* Continue processing this key normally */
	      }
	    }
	
	    /* ENHANCED F256 HARDWARE KEY PROCESSING */
            if ((key_char >= 32 && key_char < 127) && ((key_status == 0) || (key_status & SHIFT_BIT) || (key_status & SPACE_BIT))){
                /* Replace selection if active */
                if (sel_active()) {
                    del_sel();
                    update_from_pos = buf.select_start;
                } else {
                    update_from_pos = buf.cursor_pos;
                }
                    
                do_char(key_char);
              
            } else if (key_char == KEY_TAB && !(key_status & RIGHTBIT)) {
                if (sel_active()) {
                    del_sel();
                    update_from_pos = buf.select_start;
                } else {
                    update_from_pos = buf.cursor_pos;
                }
                do_char(9);
	    } else if (key_char == KEY_ENTER) {
	      if (in_search_mode) {
                    find_next();  /* Enter in search = find next */
	      } else {
		if (sel_active()) {
		  del_sel();  /* Delete selection before enter */
		  update_from_pos = buf.select_start;
		}
		/* Check for Shift+Enter to insert LF instead of CR */
		if (key_status & SHIFT_BIT) {
		  do_lf_enter();  /* Shift+Enter = insert $0A */
		} else {
		  do_cr_enter();  /* Regular Enter = insert $0D */
		}
	      }

	    } else if (key_status & SHIFT_BIT){
	      
	      if ((key_status & SHIFT_BIT) && (key_status & UPBIT) && key_char == KEY_S_UP) {
                ex_up();
	      } else if ((key_status & SHIFT_BIT) && (key_status & DOWNBIT) && key_char == KEY_S_DOWN) {
                ex_down();
	      } else if ((key_status & SHIFT_BIT) && (key_status & CTRL_BIT) && (key_status & LEFTBIT) && key_char == KEY_S_LEFT) {
                ex_wd_left();
	      } else if ((key_status & SHIFT_BIT) && (key_status & CTRL_BIT) && (key_status & RIGHTBIT) && key_char == KEY_S_RIGHT) {
                ex_wd_right();		
	      } else if ((key_status & SHIFT_BIT) && (key_status & LEFTBIT) && key_char == KEY_S_LEFT) {
                ex_left();
	      } else if ((key_status & SHIFT_BIT) && (key_status & RIGHTBIT) && key_char == KEY_S_RIGHT) {
                ex_right();
		           /* Shift+Ctrl+Arrow = Word Selection */
	      }

	    } else if (key_status & CTRL_BIT){
              /* Ctrl+Arrow = Word Navigation */
              if ((key_status & CTRL_BIT) && (key_status & LEFTBIT) && key_char == KEY_C_LEFT) {
		word_left();
	      } else if ((key_status & CTRL_BIT) && (key_status & RIGHTBIT) && key_char == KEY_C_RIGHT) {
		word_right();
	      } else if ((key_status & CTRL_BIT) && (key_status & UPBIT) && key_char == KEY_C_UP) {
		page_up();
	      } else if ((key_status & CTRL_BIT) && (key_status & DOWNBIT) && key_char == KEY_C_DOWN) {
		page_down();
		
		/* Ctrl+Letter combinations using F256 hardware detection */
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_Q) {  /* Ctrl+Q = Quit */
		if (buf.dirty && !quit_confirm) {
		  strcpy(status_msg, "File modified - ^S to save, ^Q again to quit anyway");
		  quit_confirm = 1;
		  need_status_update = 1;
		} else {
		  cleanup_clipboard();
#ifndef coco3
		  rest_chr();
#endif		  
		  if (buf.text_storage != NULL) {
		    free(buf.text_storage);
		  }
		  restore_mode();
		  write_block(1,CLEAR_SCREEN,1);
		  exit(0);
		}
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_S) {  /* Ctrl+S = Save */
		save_file();
		need_status_update = 1;
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_F) {  /* Ctrl+F = Find / Find Next */
		if (!in_search_mode && !in_find_mode) {
		  /* Start new search */
		  strtsch();
		  need_status_update = 1;
		} else if (in_find_mode) {
		  /* Find next occurrence */
		  find_next();
		}
		/* Note: in_search_mode case handled above before search_keys() */
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_G) {  /* Ctrl+G = Goto Line */
		start_goto();
		need_status_update = 1;
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_H) {  /* Ctrl+H = Help */
		show_help();
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_A) {  /* Ctrl+A = Select All */
		sel_all();
		need_full_redraw = 1;
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_C) {  /* Ctrl+C = COPY */
		if (sel_active()) {
		  copy_sel();
		  strcpy(status_msg, "Copied to clipboard");
		  temp_message_active = 1;
		} else {
		  strcpy(status_msg, "Nothing selected to copy");
		}
		need_status_update = 1;
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_X) {  /* Ctrl+X = CUT */
		if (sel_active()) {
		  copy_sel();
		  del_sel();
		  strcpy(status_msg, "Cut to clipboard");
		  need_status_update = 1;
		  need_minimal_update = 1;
		  update_from_pos = buf.select_start;
		} else {
		  strcpy(status_msg, "Nothing selected to cut");
		  need_status_update = 1;
		}
	      
	      
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_V) {  /* Ctrl+V = PASTE */
		paste_clipboard();
		need_status_update = 1;
	      
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_Z) {  /* Ctrl+Z = UNDO */
		do_undo();
		need_full_redraw = 1;
		need_status_update = 1;
	      
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_1) {  /* Ctrl+1 = Single-spacing */
		dbl_space = 0;
		eff_rows = status_row - text_start_row;
		need_full_redraw = 1;
		strcpy(status_msg, "Single-spacing");
		need_status_update = 1;
	      
	      } else if ((key_status & CTRL_BIT) && key_char == KEY_C_2) {  /* Ctrl+2 = Double-spacing */
		dbl_space = 1;
		eff_rows = (status_row - text_start_row) / 2;
		need_full_redraw = 1;
		strcpy(status_msg, "Double-spacing");
		need_status_update = 1;
	      
	      }
	      
            /* Regular Arrow Keys with Visual Line Navigation */
            } else if (key_status & UPBIT && key_char == KEY_UP) {
		if (sel_active()) {
		    /* With selection: jump to start and clear */
		    set_curs(buf.select_start);
		    clr_sel();
		    ensure_vis();
		} else {
		    /* Normal movement */
		    move_up();
		}
		need_status_update = 1;
            } else if (key_status & DOWNBIT && key_char == KEY_DOWN) {
		if (sel_active()) {
		    /* With selection: jump to end and clear */
		    set_curs(buf.select_end);
		    clr_sel();
		    ensure_vis();
		} else {
		    /* Normal movement */
		    move_down();
		}
		need_status_update = 1;
	    } else if (key_status & LEFTBIT && key_char == KEY_LEFT) {
		if (sel_active()) {
		    /* With selection: jump to start and clear */
		    set_curs(buf.select_start);
		    clr_sel();
		    ensure_vis();
		} else {
		    /* Normal movement */
		    move_left();
		}
		need_status_update = 1;
            } else if (key_status & RIGHTBIT && key_char == KEY_RIGHT) {
		if (sel_active()) {
		    /* With selection: jump to end and clear */
		    set_curs(buf.select_end);
		    clr_sel();
		    ensure_vis();
		} else {
		    /* Normal movement */
		    move_right();
		}
		need_status_update = 1;

            } else if (key_char == 127 || ((key_char == KEY_BS) && !(key_status & LEFTBIT))) {
	      /* Backspace - handle selection */
	      if (sel_active()) {
		del_sel();
		need_minimal_update = 1;
		update_from_pos = buf.select_start;
	      } else {
		update_from_pos = buf.cursor_pos - 1;
		do_back();
		need_minimal_update = 1;
	      }
            } else if (key_char == 127 && (key_status & SHIFT_BIT)) {  /* Shift+Del */
	      if (sel_active()) {
		copy_sel();  /* Copy to clipboard (simulated) */
		del_sel();
		strcpy(status_msg, "Cut to clipboard");
		need_status_update = 1;
	      }
            } else if (key_char == KEY_ESC) {
	      if (in_search_mode) {
		end_search();
		need_status_update = 1;
	      } else if (buf.selecting) {
		clr_sel();
		strcpy(status_msg, "Selection cleared");
		need_status_update = 1;
	      }
            } else if (key_char == KEY_C_C) {
	      restore_mode();
	      exit(0);
            } else if (key_char == KEY_C_S) {
	      save_file();
	      need_status_update = 1;
            } 
	    
            /* Clear quit confirmation on any other key */
            if (key_char != KEY_C_Q) {
	      quit_confirm = 0;
            }
            
            upd_fast();
        }
    }
    
    restore_mode();
}

/* ENHANCED MOVEMENT FUNCTIONS WITH SOFT WRAP SUPPORT */

/* Helper: Advance column by one character, handling tabs */
col_adv(col, ch)
int col;
char ch;
{
    if (ch == 9) {
        return NEXT_TAB(col);
    }
    return col + 1;
}

/* Helper: Find where visual line ends from start position */
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

/* Helper: Find position at target column on visual line */
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

/* Ultra-simple move_up using visln_pre() */
move_up()
{
    int target, ch;
    int line_end;
    int at_line_end;
    
    /* Check if we're at a line terminator */
    at_line_end = 0;
    if (buf.cursor_pos < buf.text_length) {
        ch = gap_char_at(buf.cursor_pos);
        if (IS_LINE_END(ch)) {
            at_line_end = 1;
        }
    }
    
    /* Get previous visual line - visln_pre handles all cases correctly */
    target = visln_pre(buf.cursor_pos);
    
    if (target >= buf.cursor_pos) return;  /* Can't go up */
    
    /* Find where this visual line ends */
    line_end = find_end(target);
    
    /* Position cursor at target column */
    target = pos_col(target, cursor_col, line_end);
    
    /* If original position was at line end and target line is shorter,
       position at end of target line */
    if (at_line_end && target >= line_end) {
        target = line_end;
    }
    
    set_curs(target);
    log_row--;
}

move_down()
{
    int target, ch;
    int line_end;
    int at_line_end;
    
    /* Check if we're at a line terminator */
    at_line_end = 0;
    if (buf.cursor_pos < buf.text_length) {
        ch = gap_char_at(buf.cursor_pos);
        if (IS_LINE_END(ch)) {
            at_line_end = 1;
        }
    }
    

    target = visln_next(buf.cursor_pos);

    
    if (target <= buf.cursor_pos) return;  /* Can't go down */
    
    /* Find where this visual line ends */
    line_end = find_end(target);
    
    /* Position cursor at target column */
    target = pos_col(target, cursor_col, line_end);
    
    /* If original position was at line end and target line is shorter,
       position at end of target line */
    if (at_line_end && target >= line_end) {
        target = line_end;
    }
    
    set_curs(target);
    log_row++;
}

move_left()
{
    if (buf.cursor_pos > 0) {
        curs_lft();
    }
}

move_right()
{
    if (buf.cursor_pos < buf.text_length) {
        curs_rgt();
    }
}

set_curs(new_pos)
int new_pos;
{
    int old_pos, i;
    
    if (new_pos < 0) new_pos = 0;
    if (new_pos > buf.text_length) new_pos = buf.text_length;
    
    if (new_pos == buf.cursor_pos) return;  /* No change */
    
    old_pos = buf.cursor_pos;
    buf.cursor_pos = new_pos;

    
    /* Update cached line number incrementally */
        if (new_pos > old_pos) {
            /* Moving forward - count newlines crossed */
            for (i = old_pos; i < new_pos; i++) {
                if (IS_LINE_END(gap_char_at(i))) {
                    buf.ccurs_ln++;
                }
            }
        } else {
            /* Moving backward - count newlines crossed */
            for (i = old_pos - 1; i >= new_pos; i--) {
                if (IS_LINE_END(gap_char_at(i))) {
                    buf.ccurs_ln--;
                }
            }
        }
}

/* Calculate column position for a given buffer position */
/* Returns the column (0-79) where the cursor would be at this position */
calc_col(pos)
int pos;
{
    int col, i;
    int line_start;
    char ch;
    
    /* Find start of current visual line */
    line_start = visln_sta(pos);
    
    /* Count columns from line start to pos */
    col = 0;
    for (i = line_start; i < pos && i < buf.text_length; i++) {
        ch = gap_char_at(i);
        if (!IS_LINE_END(ch)) {
            col = col_adv(col, ch);
            if (col >= screen_cols) {
                /* Wrapped to next visual line */
                line_start = i + 1;
                col = 0;
            }
        }
    }
    
    return col;
}

/* Update cursor position moving left with proper screen coordinate tracking */
curs_lft()
{
    char prev_char;
    int old_log_row;
    
    if (buf.cursor_pos <= 0) return;
    
    prev_char = gap_char_at(buf.cursor_pos - 1);
    old_log_row = log_row;
    
    /* Update buffer position and line number */
    set_curs(buf.cursor_pos - 1);
    
    /* Update screen coordinates based on character type */
    if (IS_LINE_END(prev_char)) {
        /* Moved to previous logical line - need to recalculate column */
        log_row--;
        cursor_col = calc_col(buf.cursor_pos);
    } else if (prev_char == 9) {
        /* Tab - need to recalculate column since tabs are variable width */
        cursor_col = calc_col(buf.cursor_pos);
        /* Check if the new position is on a different visual line */
        /* This can happen if the tab crossed a visual line boundary */
        if (cursor_col == 0 && old_log_row > 0) {
            log_row--;
            /* cursor_col is already 0, no need to recalculate */
        }
    } else {
        /* Regular character - simple decrement */
        cursor_col--;
        if (cursor_col < 0) {
            /* Wrapped to previous visual line */
            log_row--;
            cursor_col = screen_cols - 1;
        }
    }
}

/* Update cursor position moving right with proper screen coordinate tracking */
curs_rgt()
{
    char curr_char;
    
    if (buf.cursor_pos >= buf.text_length) return;
    
    curr_char = gap_char_at(buf.cursor_pos);
    
    /* Update buffer position and line number */
    set_curs(buf.cursor_pos + 1);
    
    /* Update screen coordinates based on character type */
    if (IS_LINE_END(curr_char)) {
        /* Moved to next logical line */
        log_row++;
        cursor_col = 0;
    } else {
        /* Advance column (handles tabs and regular chars) */
        cursor_col = col_adv(cursor_col, curr_char);
        if (cursor_col >= screen_cols) {
            /* Wrapped to next visual line */
            log_row++;
            cursor_col = 0;
        }
    }
}

/* Enhanced status line with right-aligned position info */
draw_stat()
{
    static char cached_status_msg[80] = "";
    static int stinit = 0;  /* Renamed from status_msg_initialized to avoid 8-char conflict */
    static int last_right_len = 0;
    
    char right_buf[50];
    int right_len;
    int left_len;
    int padding;
    int i;
    int status_changed;
    int pad_needed;
    
    /* Hide cursor while drawing status and help lines */
    write_block(1, HIDE_CURSOR, 2);
    
    /* Build right side with position info */
    if (sel_active()) {
        sprintf(right_buf, "S:%d L:%d/%d C:%d %dK", 
                buf.select_end - buf.select_start,
                buf.ccurs_ln + 1,
                total_logical_lines,
                cursor_col,
                buf.text_length / 1024);
    } else {
        sprintf(right_buf, "L:%d/%d C:%d %dK",
                buf.ccurs_ln + 1,
                total_logical_lines,
                cursor_col,
                buf.text_length / 1024);
    }
    
    right_len = strlen(right_buf);
    
    /* Pad right_buf if shorter than last time to cover old text */
    pad_needed = last_right_len - right_len;
    if (pad_needed > 0) {
        for (i = 0; i < pad_needed; i++) {
            right_buf[right_len + i] = ' ';
        }
        right_buf[right_len + pad_needed] = '\0';
        right_len = last_right_len;
    }
    
    /* Check if status message changed and set temp_message_active */
    if (strlen(status_msg) > 0) {
        temp_message_active = 1;
    } else {
        temp_message_active = 0;
    }
    status_changed = strcmp(status_msg, cached_status_msg) != 0;
    
    /* Turn on reverse video */
    write_block(1, REV_ON, 2);
    
    /* Only do full redraw with CLEAR_EOL if status message changed or first time */
    if (status_changed || !stinit || need_full_redraw) {
        /* Position cursor to start of status line */
        putchar(0x02);
        putchar(0 + 0x20);
        putchar(status_row + 0x20);
        
        /* Clear entire line */
        write_block(1, CLEAR_EOL, 1);
        
        /* Reposition to start of line (cursor may have moved after CLEAR_EOL) */
        putchar(0x02);
        putchar(0 + 0x20);
        putchar(status_row + 0x20);
        
        /* Write message on left if present */
        if (temp_message_active) {
            left_len = strlen(status_msg);
            
            /* Check if message + right side would overflow */
            if (left_len + right_len >= screen_cols) {
                /* Too long - truncate message */
                left_len = screen_cols - right_len - 1;
                if (left_len < 0) left_len = 0;
            }
            
            /* Write only the amount that fits */
            write_block(1, status_msg, left_len);
            strcpy(cached_status_msg, status_msg);
            strcpy(status_msg, "");
        } else {
            strcpy(cached_status_msg, "");
        }
        
        /* Position cursor to right side and write position info */
        putchar(0x02);
        putchar((screen_cols - right_len - 1) + 0x20);
        putchar(status_row + 0x20);
        write_str(right_buf);
        
        stinit = 1;
    }
    else {
        /* Status message unchanged - only update right side (no CLEAR_EOL = no flash!) */
        putchar(0x02);
        putchar((screen_cols - right_len - 1) + 0x20);
        putchar(status_row + 0x20);
        write_str(right_buf);
    }
    
    write_block(1, REV_OFF, 2);
    
    /* Remember right_len for next time to handle padding */
    last_right_len = right_len;
    
    /* Enhanced help line - simplified with full help screen available */
    putchar(0x02);
    putchar(0 + 0x20);
    putchar((status_row + 1) + 0x20);
#ifdef coco3
    if (in_search_mode) {
        printf("Ctrl+F=Find  Enter=Find  F1=Cancel");
    } else if (in_goto_mode) {
        printf("Enter line number  Enter=Go  F1=Cancel");
    } else if (in_find_mode) {
        printf("Ctrl+F=Find Next  Start typing to exit find mode");
    } else {
        printf("Ctrl+H for Help");
    }
#else    
    if (in_search_mode) {
        printf("Ctrl+F=Find  Enter=Find  ESC=Cancel");
    } else if (in_goto_mode) {
        printf("Enter line number  Enter=Go  ESC=Cancel");
    } else if (in_find_mode) {
        printf("Ctrl+F=Find Next  Start typing to exit find mode");
    } else {
        printf("Ctrl+H for Help");
    }
#endif    
    /* Clear to end of help line */
    write_block(1, CLEAR_EOL, 1);
    
    /* Show cursor again */
    write_block(1, SHOW_CURSOR, 2);
}

/* Help Screen Functions */

/* Help command data structure */
struct HelpCmd {
    char *full;     /* Full format for tall screens */
    char *compact;  /* Compact format for short screens */
    char cat;       /* Category: 'F', 'E', 'S', 'C', 'N', 'D', or 'H' for header */
};

/* Help table - category headers have cat='H' */
struct HelpCmd help_table[] = {
    {"FILE OPERATIONS:", "FILE:", 'H'},
    {"  ^S              Save file", "^S=Save", 'F'},
    {"  ^Q              Quit (confirms if unsaved)", "^Q=Quit", 'F'},
    {"", "", 'F'},
    
    {"EDITING:", "EDIT:", 'H'},
    {"  ^Z              Undo (50 levels)", "^Z=Undo", 'E'},
#ifdef coco3    
    {"  Break=Backspace       Delete character before cursor", "Break=Del<", 'E'},
#else
    {"  Backspace       Delete character before cursor", "BS=Del<", 'E'},
#endif    
    {"  Delete          Delete character at cursor", "Del=Del>", 'E'},
    {"  Tab             Insert tab character", "Tab=Tab", 'E'},
    {"", "", 'E'},
    
    {"SELECTION:", "SELECT:", 'H'},
    {"  ^A              Select all", "^A=All", 'S'},
    {"  Shift+Arrows    Select text", "Shft+Arr=Sel", 'S'},
    {"  Shift+^Arrows   Select by word", "Shft+^Arr=Word", 'S'},
    {"  ESC             Clear selection", "ESC=Clear", 'S'},
    {"", "", 'S'},
    
    {"CLIPBOARD:", "CLIP:", 'H'},
    {"  ^C              Copy selection", "^C=Copy", 'C'},
    {"  ^X              Cut selection", "^X=Cut", 'C'},
    {"  ^V              Paste from clipboard", "^V=Paste", 'C'},
    {"", "", 'C'},
    
    {"SEARCH & NAVIGATION:", "NAV:", 'H'},
    {"  ^F              Find text / Find next", "^F=Find", 'N'},
    {"  ^G              Go to line number", "^G=Goto", 'N'},
    {"  ^Arrows         Move by word", "^Arr=Word", 'N'},
    {"  ^Up/Down        Page up/down", "^Up/Dn=Page", 'N'},
    {"", "", 'N'},
    
    {"DISPLAY:", "VIEW:", 'H'},
#ifdef coco3
    {"  ^F1              Single-spacing mode", "^F1=Single", 'D'},
    {"  ^F2              Double-spacing mode", "^F2=Double", 'D'},
#else    
    {"  ^1              Single-spacing mode", "^1=Single", 'D'},
    {"  ^2              Double-spacing mode", "^2=Double", 'D'},
#endif
    {"", "", 'D'},
    
    {NULL, NULL, 0}
};

/* Helper to write string using write_block */
write_str(s)
char *s;
{
    write_block(1, s, strlen(s));
}

/* Display full-screen help overlay - adapts to screen size */
show_help()
{
    int i;
    
    in_help_mode = 1;
    
    write_block(1, CLEAR_SCREEN, 1);
    write_block(1, HOME_CURSOR, 1);
    
    write_str("\n");
    write_block(1, REV_ON, 2);
    
    /* Choose format based on screen rows */
    if (screen_rows >= 60) {
        /* Double-spaced full format for very tall screens (80x60+) */
        write_str("  F256 Text Editor - Help  ");
        write_block(1, REV_OFF, 2);
        write_str("\n\n");
        
        for (i = 0; help_table[i].full != NULL; i++) {
            /* Skip blank lines - they're just separators in the table */
            if (help_table[i].full[0] != 0) {
                write_str(help_table[i].full);
                write_str("\n");
                /* Add blank line after every line for readability */
                write_str("\n");
            }
        }
        
        /* No extra blank before footer - save space */
        write_block(1, REV_ON, 2);
        write_str("Press any key to return to editor");
        
    } else if (screen_rows >= 40) {
        /* Regular full format for tall screens (80x40-80x59) */
        write_str("  F256 Text Editor - Help  ");
        write_block(1, REV_OFF, 2);
        write_str("\n\n");
        
        for (i = 0; help_table[i].full != NULL; i++) {
            write_str(help_table[i].full);
            write_str("\n");
        }
        
        write_str("\n");
        write_block(1, REV_ON, 2);
        write_str("Press any key to return to editor");
        
    } else {
        /* Compact format for short screens (80x30, 80x24, 40x30) */
        int first_in_cat;
        char *cat_name;
        int cat_len;
        int spaces_needed;
        int j;
        
        write_str(" Help ");
        write_block(1, REV_OFF, 2);
        write_str("\n\n");
        
        first_in_cat = 0;
        for (i = 0; help_table[i].compact != NULL; i++) {
            if (help_table[i].cat == 'H') {
                /* Category header - calculate spacing to align at column 9 */
                write_str("\n");
                cat_name = help_table[i].compact;
                cat_len = strlen(cat_name);
                write_str(cat_name);
                
                /* Pad to column 9 (category + spaces = 9) */
                spaces_needed = 9 - cat_len;
                for (j = 0; j < spaces_needed; j++) {
                    write_str(" ");
                }
                
                first_in_cat = 1;
            } else if (help_table[i].compact[0] != 0) {
                /* Command - first one on same line, rest indented to column 9 */
                if (first_in_cat) {
                    /* First command after category - on same line */
                    write_str(help_table[i].compact);
                    first_in_cat = 0;
                } else {
                    /* Subsequent commands - on new line, indented to column 9 */
                    write_str("\n");
                    write_str("         ");
                    write_str(help_table[i].compact);
                }
            }
        }
        
        write_str("\n\n");
        write_block(1, REV_ON, 2);
        write_str("Any key=exit");
    }
    
    write_block(1, REV_OFF, 2);
}

/* Hide help and return to editor */
hide_help()
{
    in_help_mode = 0;
    need_full_redraw = 1;
    upd_fast();
}

/* Text Selection Functions */

/* Start selection at current cursor */
start_sel()
{
    if (!buf.selecting) {
        buf.selecting = 1;
        buf.select_start = buf.cursor_pos;
        buf.select_end = buf.cursor_pos;
        buf.selection_anchor = buf.cursor_pos;
    }
}

/* Clear selection */
clr_sel()
{
    if (buf.selecting) {
        buf.selecting = 0;
        buf.select_start = -1;
        buf.select_end = -1;
        buf.selection_anchor = -1;
        need_full_redraw = 1;  /* Need to redraw to clear highlights */
    }
}

smart_sel_update()
{
    int anchor = buf.selection_anchor;
    int cursor = buf.cursor_pos;
    
    /* Determine boundaries based on cursor position relative to anchor */
    if (cursor < anchor) {
        /* Cursor is left of anchor - selection extends left */
        buf.select_start = cursor;
        buf.select_end = anchor;
        sprintf(status_msg, "Selection LEFT: %d-%d (len=%d)", 
                buf.select_start, buf.select_end, buf.select_end - buf.select_start);
        
    } else if (cursor > anchor) {
        /* Cursor is right of anchor - selection extends right */
        buf.select_start = anchor;
        buf.select_end = cursor;
        sprintf(status_msg, "Selection RIGHT: %d-%d (len=%d)", 
                buf.select_start, buf.select_end, buf.select_end - buf.select_start);
        
    } else {
        /* Cursor is at anchor - clear selection (zero-length not useful) */
        buf.selecting = 0;
        buf.select_start = -1;
        buf.select_end = -1;
        buf.selection_anchor = -1;
        sprintf(status_msg, "Selection cleared");
    }
}


/* Check if selection is active and valid */
sel_active()
{
    return (buf.selecting && buf.select_start >= 0 && 
            buf.select_end > buf.select_start);
}

/* Selection movement functions with screen coordinate updates */

/* Extend selection left - fixed to use proper cursor management */
ex_left()
{
  int old_pos;
  int old_col;
  int old_row;
  char ch;
  
  if (!buf.selecting) start_sel();
    
  if (buf.cursor_pos > 0) {
    /* Save current position before moving */
    old_pos = buf.cursor_pos;
    old_col = cursor_col;
    old_row = log_row;
    ch = gap_char_at(buf.cursor_pos - 1);  /* Check the character we're moving to */
    
    /* Use proper cursor update that handles tabs and line endings */
    curs_lft();
        
    /* Use smart update - no parameters needed, uses cursor_pos and anchor */
    smart_sel_update();
    
    /* Check for boundary conditions */
    if (IS_LINE_END(ch) || cursor_col == 0 || cursor_col == screen_cols - 1) {
      /* Complex case - use broader update */
      need_full_redraw = 1;
    } else {
      /* Simple case - use fast character updates */
      int phys_old_row, phys_log_row;
      phys_old_row = PHYS_ROW(old_row);
      phys_log_row = PHYS_ROW(log_row);
      redraw_char_at_screen_pos(old_col, phys_old_row, old_pos);
      redraw_char_at_screen_pos(cursor_col, phys_log_row, buf.cursor_pos);
    }     
  }
  
  need_status_update = 1;
}

/* Extend selection right - fixed to use proper cursor management */
ex_right()
{
  int old_pos;
  int old_col;
  int old_row;
  char ch;
  
  if (!buf.selecting) start_sel();
    
  if (buf.cursor_pos < buf.text_length) {
    /* Save current position before moving */
    old_pos = buf.cursor_pos;
    old_col = cursor_col;
    old_row = log_row;
    ch = gap_char_at(buf.cursor_pos);  /* Check the character we're moving over */
    
    /* Use proper cursor update that handles tabs and line endings */
    curs_rgt();
        
    /* Use smart update - no parameters needed, uses cursor_pos and anchor */
    smart_sel_update();
    
    /* Check for boundary conditions */
    if (IS_LINE_END(ch) || cursor_col == 0 || cursor_col == screen_cols - 1) {
      /* Complex case - use broader update */
      need_full_redraw = 1;
    } else {
      /* Simple case - use fast character updates */
      int phys_old_row, phys_log_row;
      phys_old_row = PHYS_ROW(old_row);
      phys_log_row = PHYS_ROW(log_row);
      redraw_char_at_screen_pos(old_col, phys_old_row, old_pos);
      redraw_char_at_screen_pos(cursor_col, phys_log_row, buf.cursor_pos);
    }
  }
  
  need_status_update = 1;
}

/* Extend selection up */
ex_up()
{
    if (!buf.selecting) start_sel();
    move_up();
    smart_sel_update();
    need_full_redraw = 1;
    need_status_update = 1;
}

/* Extend selection down */
ex_down()
{
    if (!buf.selecting) start_sel();
    move_down();
    smart_sel_update();
    need_full_redraw = 1;
    need_status_update = 1;
}

/* Extend selection word left */
ex_wd_left()
{
    if (!buf.selecting) start_sel();
    word_left();  /* Use existing word movement function */
    smart_sel_update();
    need_full_redraw = 1;
    need_status_update = 1;
}

/* Extend selection word right */
ex_wd_right()
{
    if (!buf.selecting) start_sel();
    word_right(); /* Use existing word movement function */
    smart_sel_update();
    need_full_redraw = 1;
    need_status_update = 1;
}

/* Select all text */
sel_all()
{
    buf.selecting = 1;
    buf.selection_anchor = 0;
    buf.select_start = 0;
    buf.select_end = buf.text_length;
    buf.cursor_pos = buf.text_length;
    strcpy(status_msg, "All text selected");
    need_status_update = 1;
}

/* Delete selected text using gap buffer operations */
del_sel()
{
    int del_len;
    
    if (!buf.selecting || buf.select_start < 0 || buf.select_end <= buf.select_start) {
        return;  /* No valid selection */
    }
    
    del_len = buf.select_end - buf.select_start;
    
    /* Record each deleted character for undo (up to 50 chars) */
    /* Read characters before deleting them */
    {
        int i, undo_count_limit;
        char deleted_char;
        
        /* Limit to 50 characters to fit in undo buffer */
        undo_count_limit = del_len;
        if (undo_count_limit > MAX_UNDO) {
            undo_count_limit = MAX_UNDO;
        }
        
        /* Record each character that will be deleted */
        for (i = 0; i < undo_count_limit; i++) {
            deleted_char = gap_char_at(buf.select_start + i);
            add_undo(buf.select_start + i, 1, deleted_char);
        }
    }
    
    /* Move gap to selection start position */
    move_gap_to(buf.select_start);
    
    /* Expand gap to cover the selected text - this "deletes" it */
    buf.gap_end += del_len;
    
    /* Validate gap doesn't exceed buffer bounds */
    if (buf.gap_end > BUF_SIZE) {
        buf.gap_end = BUF_SIZE;
    }
    
    /* Update buffer state */
    buf.text_length -= del_len;
    buf.cursor_pos = buf.select_start;
    set_dirty(1);
    
    /* Recalculate total lines after deleting selection */
    recount_total_lines();
    
    /* Clear selection state */
    clr_sel();
}

/* Replace the existing copy_sel() function around line 1200 */
copy_sel()
{
    int sel_len;
    
    if (!sel_active()) {
        strcpy(status_msg, "Nothing selected to copy");
        need_status_update = 1;
        return;
    }
    
    if (!clipboard.initialized) {
        strcpy(status_msg, "No clipboard available");
        need_status_update = 1;
        return;
    }
    
    sel_len = buf.select_end - buf.select_start;
    if (sel_len <= 0) return;
    
    /* Check if selection fits in 8K clipboard */
    if (sel_len > clipboard.max_capacity) {
        sprintf(status_msg, "Selection too large (%d chars, max 8K)", sel_len);
        need_status_update = 1;
        return;
    }
    
    /* Copy selection to fixed clipboard */
    copy_gap_selection_to_clipboard(sel_len);
    clipboard.data_length = sel_len;
    sprintf(status_msg, "Copied %d chars to clipboard", sel_len);
    need_status_update = 1;
}

/* Add this new function after copy_sel() */
paste_clipboard()
{
    int i;
    
    if (!clipboard.initialized) {
        strcpy(status_msg, "No clipboard available");
        need_status_update = 1;
        return;
    }
    
    if (clipboard.data_length <= 0) {
        strcpy(status_msg, "Clipboard empty");
        need_status_update = 1;
        return;
    }
    
    /* Replace selection if active */
    if (sel_active()) {
        del_sel();
    }
    
    /* Insert clipboard contents at cursor */
    for (i = 0; i < clipboard.data_length; i++) {
        if (!gap_has_space()) {
            sprintf(status_msg, "Buffer full - pasted %d of %d chars", i, clipboard.data_length);
            need_status_update = 1;
            break;
        }
        
        ensure_gap_at_cursor();
        *(text_ptr + buf.gap_start) = clipboard.mapped_addr[i];
        buf.gap_start++;
        buf.cursor_pos++;
        buf.text_length++;
    }
    
    set_dirty(1);
    sprintf(status_msg, "Pasted %d chars from clipboard", clipboard.data_length);
    temp_message_active = 1;
    recount_total_lines();  /* Recalculate after paste */
    need_redraw_down = 1;
    update_from_pos = buf.cursor_pos - clipboard.data_length;
    need_status_update = 1;
}

/* Word Movement Functions */

/* Check if character is part of a word */
is_word_char(ch)
char ch;
{
    return ((ch >= 'a' && ch <= 'z') || 
            (ch >= 'A' && ch <= 'Z') || 
            (ch >= '0' && ch <= '9') || 
            ch == '_');
}

/* Find start of current word */
fdwd_start(pos)
int pos;
{
    /* Skip non-word chars backwards */
    while (pos > 0 && !is_word_char(*(text_ptr + pos - 1))) {
        pos = pos - 1;
    }
    
    /* Find start of word */
    while (pos > 0 && is_word_char(*(text_ptr + pos - 1))) {
        pos = pos - 1;
    }
    
    return pos;
}

/* Find end of current word */
fdwd_end(pos)
int pos;
{
    /* Skip non-word chars forward */
    while (pos < buf.text_length && !is_word_char(*(text_ptr + pos))) {
        pos = pos + 1;
    }
    
    /* Find end of word */
    while (pos < buf.text_length && is_word_char(*(text_ptr + pos))) {
        pos = pos + 1;
    }
    
    return pos;
}

/* Move cursor to start of previous word */
word_left()
{
    int new_pos;
    
    if (buf.cursor_pos <= 0) return;
    
    new_pos = buf.cursor_pos - 1;
    
    while (new_pos > 0 && is_word_char(gap_char_at(new_pos))) {
        new_pos = new_pos - 1;
    }
    
    while (new_pos > 0 && !is_word_char(gap_char_at(new_pos))) {
        new_pos = new_pos - 1;
    }
    
    while (new_pos > 0 && is_word_char(gap_char_at(new_pos - 1))) {
        new_pos = new_pos - 1;
    }
    
    set_curs(new_pos);  /* Use cached update */
    /* clr_sel(); */
}

/* Move cursor to start of next word */
word_right()
{
    int new_pos;
    
    if (buf.cursor_pos >= buf.text_length) return;
    
    new_pos = buf.cursor_pos;
    
    while (new_pos < buf.text_length && is_word_char(gap_char_at(new_pos))) {
        new_pos = new_pos + 1;
    }
    
    while (new_pos < buf.text_length && !is_word_char(gap_char_at(new_pos))) {
        new_pos = new_pos + 1;
    }
    
    set_curs(new_pos);  /* Use cached update */
    /* clr_sel()'*/
}

/* Helper function to position cursor at same relative screen position */
page_curs(new_screen_top)
int new_screen_top;
{
    int i, relative_row, target_pos, next_pos;
    
    /* Only adjust if cursor is currently in text area */
    if (log_row < 0 || log_row >= eff_rows) {
        return;  /* Cursor not in text area */
    }
    
    /* Calculate which visual line cursor is on */
    relative_row = log_row;
    target_pos = new_screen_top;
    
    /* Advance to same relative visual line */
    for (i = 0; i < relative_row && target_pos < buf.text_length; i++) {
        next_pos = visln_next(target_pos);
        if (next_pos <= target_pos || next_pos > buf.text_length) break;
        target_pos = next_pos;
    }
    
    /* Ensure cursor stays within buffer bounds */
    if (target_pos > buf.text_length) {
        target_pos = buf.text_length;
    }
    
    set_curs(target_pos);
}

/* Simplified page_up using helper */
page_up()
{
    int i, new_top_pos, lines_to_scroll, prev_pos;
    int original_top_pos;

    original_top_pos = get_top_pos();
    new_top_pos = original_top_pos;
    lines_to_scroll = eff_rows;  /* Use effective rows for proper scrolling */
    
    /* Scroll up by screen height */
    for (i = 0; i < lines_to_scroll && new_top_pos > 0; i++) {
        prev_pos = new_top_pos;
        new_top_pos = visln_pre(new_top_pos);
        if (new_top_pos >= prev_pos) break;
    }
    
    if (new_top_pos < 0) new_top_pos = 0;

    /* Check if we're already at the top */
    if (new_top_pos == original_top_pos && original_top_pos == 0) {
        /* Already at top of file - just move cursor to top of screen */
        set_curs(0);
        log_row = 0;  /* First logical row */
        cursor_col = 0;
        need_full_redraw = 1;
        clr_sel();
        return;
    }
    
    /* Update screen */
    buf.topscr_pos = new_top_pos;
    need_full_redraw = 1;
    
    /* Position cursor at same relative location */
    page_curs(new_top_pos);
    clr_sel();
}

/* Simplified page_down using helper */
page_down()
{
    int i, new_top_pos, lines_to_scroll, next_pos;
    int last_line_start, max_reasonable_top;
    
    new_top_pos = get_top_pos();
    lines_to_scroll = eff_rows;  /* Use effective rows for proper scrolling */
    
    /* Calculate maximum scroll position */
    if (buf.text_length > 0) {
        last_line_start = line_sta(buf.text_length - 1);
        max_reasonable_top = last_line_start;
    } else {
        max_reasonable_top = 0;
    }
    
    /* Scroll down by screen height */
    for (i = 0; i < lines_to_scroll && new_top_pos < buf.text_length; i++) {
        next_pos = visln_next(new_top_pos);
        if (next_pos <= new_top_pos || next_pos > max_reasonable_top) break;
        new_top_pos = next_pos;
    }
    
    if (new_top_pos > buf.text_length) new_top_pos = buf.text_length;
    
    /* Update screen */
    buf.topscr_pos = new_top_pos;
    need_full_redraw = 1;
    
    /* Position cursor at same relative location */
    page_curs(new_top_pos);
    clr_sel();
}

/* Search Functions */

/* Start search mode */
strtsch()
{
    in_search_mode = 1;
    temp_search_str[0] = 0;
    strcpy(status_msg, "Find: ");
    buf.search_active = 1;
    need_status_update = 1;
}

/* End search mode */
end_search()
{
    in_search_mode = 0;
    in_find_mode = 0;
    buf.search_active = 0;
    temp_search_str[0] = 0;
    strcpy(status_msg, "Find cancelled");
    need_status_update = 1;
}

/* Handle search mode keys */
search_keys(key)
int key;
{
    int len;
    
    if (key == KEY_ESC) {
        end_search();
    } else if (key == KEY_ENTER) {
        /* Execute first find and switch to find mode */
        if (temp_search_str[0] != 0) {
            /* User typed something - use it */
            strcpy(buf.search_str, temp_search_str);
            find_next();
            in_search_mode = 0;
            in_find_mode = 1;
        } else if (buf.search_str[0] != 0) {
            /* Blank but previous search exists - reuse it */
	    strcpy(temp_search_str, buf.search_str);
            find_next();
            in_search_mode = 0;
            in_find_mode = 1;
        }
        /* Else: blank and no previous search - do nothing, stay in search mode */
    } else if (key == 127 || key == KEY_BS) {  /* Backspace */
        len = strlen(temp_search_str);
        if (len > 0) {
            temp_search_str[len - 1] = 0;
            sprintf(status_msg, "Find: %s", temp_search_str);
        }
    } else if (key >= 32 && key < 127) {
        len = strlen(temp_search_str);
        if (len < MAX_SEARCH - 1) {
            temp_search_str[len] = key;
            temp_search_str[len + 1] = 0;
            sprintf(status_msg, "Find: %s", temp_search_str);
        }
    }
}

/* Find next occurrence */
find_next()
{
    int i, j, match, search_len;
    int screen_pos, vis_lines, screen_height, is_visible;
    
    if (!buf.search_active || buf.search_str[0] == 0) {
        strcpy(status_msg, "No search string");
        need_status_update = 1;
        return;
    }
    
    search_len = strlen(buf.search_str);
    
    /* Search from current position forward */
    for (i = buf.cursor_pos + 1; i <= buf.text_length - search_len; i++) {
        match = 1;
        for (j = 0; j < search_len; j++) {
            if (gap_char_at(i + j) != buf.search_str[j]) {
                match = 0;
                break;
            }
        }
        
        if (match) {
            set_curs(i);  /* Use set_curs to update cursor and line count */
            buf.search_pos = i;
            
            /* Select the found text */
            buf.selecting = 1;
            buf.select_start = i;
            buf.select_end = i + search_len;
            buf.selection_anchor = i;
            
            /* Show find mode message */
            sprintf(status_msg, "Found: %s - Press Ctrl+F to find next", buf.search_str);

	    screen_pos = buf.topscr_pos;
	    vis_lines = 0;
	    screen_height = eff_rows;  /* Use effective rows (accounts for double-spacing) */
	    is_visible = 0;
    
	    /* Walk through visible positions counting visual lines */
	    while (screen_pos <= i && vis_lines < screen_height) {
	      if (screen_pos == i) {
		is_visible = 1;
		break;
	      }
	      screen_pos = visln_next(screen_pos);
	      vis_lines++;
	      if (screen_pos <= i && screen_pos >= buf.text_length) break;
	    }
    
	    /* If found text is off-screen, scroll to show it */
	    if (!is_visible) {
	      /* Center the found text on screen */
	      buf.topscr_pos = line_sta(i);
	      /* Move back a few visual lines for context */
	      for (j = 0; j < 5 && buf.topscr_pos > 0; j++) {
		buf.topscr_pos = visln_pre(buf.topscr_pos);
	      }
	    }
	    
            need_full_redraw = 1;
            return;
        }
    }
    
    /* Not found - exit find mode */
    sprintf(status_msg, "Not found: %s", buf.search_str);
    in_search_mode = 0;
    in_find_mode = 0;
    buf.search_active = 0;
    need_status_update = 1;
    need_full_redraw = 1;
}

/* Goto Line Functions */

/* Start goto line mode */
start_goto()
{
    in_goto_mode = 1;
    goto_line_str[0] = 0;  /* Clear line number string */
    strcpy(status_msg, "Goto line: ");
    need_status_update = 1;
}

/* End goto line mode */
end_goto()
{
    in_goto_mode = 0;
    strcpy(status_msg, "Goto cancelled");
    need_status_update = 1;
}

/* Handle goto mode keys */
goto_keys(key)
int key;
{
    int len, line_num;
    
    if (key == KEY_ESC) {
        end_goto();
    } else if (key == KEY_ENTER) {
        /* Execute goto */
        if (goto_line_str[0] != 0) {
            line_num = atoi(goto_line_str);
            if (line_num > 0) {
                goto_ln(line_num - 1);  /* goto_ln expects 0-based */
                need_full_redraw = 1;
            } else {
                strcpy(status_msg, "Invalid line number");
                need_status_update = 1;
            }
        }
        in_goto_mode = 0;
    } else if (key == 127 || key == 8) {  /* Backspace */
        len = strlen(goto_line_str);
        if (len > 0) {
            goto_line_str[len - 1] = 0;
            sprintf(status_msg, "Goto line: %s", goto_line_str);
        }
    } else if (key >= '0' && key <= '9') {  /* Only digits */
        len = strlen(goto_line_str);
        if (len < 7) {  /* Max 7 digits (9999999) */
            goto_line_str[len] = key;
            goto_line_str[len + 1] = 0;
            sprintf(status_msg, "Goto line: %s", goto_line_str);
        }
    }
}

/* Utility functions */
get_line(pos)
int pos;
{
   int i, line;
    
    line = 0;
    for (i = 0; i < pos && i < buf.text_length; i++) {
        if (IS_LINE_END(gap_char_at(i))) {
            line = line + 1;
        }
    }
    return line;
}

line_sta(pos)
int pos;
{
  while (pos > 0 && !IS_LINE_END(gap_char_at(pos - 1))) {
        pos = pos - 1;
    }
    return pos;
}

line_end(pos)
int pos;
{
   while (pos < buf.text_length && !IS_LINE_END(gap_char_at(pos))) {
        pos = pos + 1;
    }
    return pos;
}

vis_col(ln_start, pos)
int ln_start, pos;
{
  int col, i;
    char ch;
    
    col = 0;
    for (i = ln_start; i < pos && i < buf.text_length; i++) {
        ch = gap_char_at(i);
        if (ch == 9) {
            col = NEXT_TAB(col);
        } else {
            col = col + 1;
        }
    }
    
    return col;
}

do_char(ch)
int ch;
{

  int old_end_col;
  int tab_width;
  /* Always minimal update for regular characters */
  /*need_minimal_update = 1;*/
    
  ensure_gap_at_cursor();
    
  if (!gap_has_space()) {
    strcpy(status_msg, "Buffer full (16K limit)");
    need_status_update = 1;
    return;
  }
    
  add_undo(buf.cursor_pos, 0, ch);
    
  /* Insert character */
  *(text_ptr + buf.gap_start) = ch;
  buf.gap_start++;
  buf.cursor_pos++;
  buf.text_length++;
  set_dirty(1);
  
  /* Track total lines when inserting newline */
  if (IS_LINE_END(ch)) {
      total_logical_lines++;
  }
    
  /* Simple cursor tracking for regular characters */
  if (ch == 9) {
    /* Tab - calculate width for cur_end_col tracking */
    tab_width = calc_tab_width(cursor_col);
    old_end_col = cur_end_col;
    cur_end_col += tab_width;
    
    /* Check if tab caused wrapping change */
    if (cur_end_col >= screen_cols) {
        cur_end_col = cur_end_col - screen_cols;
        need_redraw_down = 1;
        need_minimal_update = 0;
        need_char_update = 0;
    } else {
        /* Tab didn't cause wrap - use minimal update */
        need_minimal_update = 1;
        need_redraw_down = 0;
        need_char_update = 0;
    }
    
    /* Don't manually adjust cursor_col/log_row - let fast_curs() do it */
    
  } else {
    /* Regular character */
    old_end_col = cur_end_col;
    cur_end_col++;
      
    cursor_col++;
    if (cursor_col >= screen_cols) {
      log_row++;
      cursor_col = 0;
    }

    /* Handle cur_end_col wrapping */
    if (cur_end_col >= screen_cols) {
        cur_end_col = cur_end_col - screen_cols;  /* Wrapped to next visual line */
        need_redraw_down = 1;  /* Wrapping changed */
        need_char_update = 0;
    } else {
        need_char_update = 1;
        need_redraw_down = 0;
    }

	
  }


  update_from_pos = buf.cursor_pos - 1;
  need_status_update = 1;
}

do_back()
{
     char deleted_char;
     int old_end_col;
    
    if (buf.cursor_pos <= 0) return;
    
    ensure_gap_at_cursor();
    
    deleted_char = *(text_ptr + buf.gap_start - 1);
    add_undo(buf.cursor_pos - 1, 1, deleted_char);
    
    /* Delete character */
    buf.gap_start--;
    buf.cursor_pos--;
    buf.text_length--;
    set_dirty(1);
    
    /* Track total lines when deleting newline */
    if (IS_LINE_END(deleted_char)) {
        total_logical_lines--;
    }
    
      /* Special case: if buffer is now empty, force full redraw */
    if (buf.text_length == 0) {
        need_full_redraw = 1;
        need_status_update = 1;
        return;
    }
    
    /* Incremental cursor tracking - move backward */
    if (IS_LINE_END(deleted_char)) {
      /* Deleted newline - complex case, need to recalculate */
      fast_curs();  

        buf.ccurs_ln--;

    
      /* For newline deletion, just force full redraw - more reliable */
      need_minimal_update = 0;
      need_redraw_down = 0;
      need_full_redraw = 1;
      update_from_pos = buf.cursor_pos - 1;     
    } else if (deleted_char == 9) {
      /* Deleted tab */
      
      /* If line ending is close to start of visual line, we're un-wrapping */
      if (cur_end_col < tab_wdth) {
        need_redraw_down = 1;  /* Wrapping changed */
        need_minimal_update = 0;
        need_char_update = 0;
      } else {
        /* Tab didn't cause wrap change - use minimal update */
        need_minimal_update = 1;
        need_redraw_down = 0;
        need_char_update = 0;
      }
      
      /* Don't manually adjust cursor_col/log_row - let fast_curs() do it */
      
      /* Redraw from current cursor position */
      update_from_pos = buf.cursor_pos;
    } else {
      old_end_col = cur_end_col;/* Regular character - simple backtrack */
      cursor_col--;
      cur_end_col--;
      if (cursor_col < 0) {
	log_row--;
	cursor_col = screen_cols - 1;  /* Wrap to end of previous line */
      }

      /* Handle cur_end_col un-wrapping */
      if (cur_end_col < 0) {
	cur_end_col = screen_cols - 1;  /* Un-wrapped from previous visual line */
        need_redraw_down = 1;  /* Wrapping changed */
        need_char_update = 0;
      } else {
        need_char_update = 1;
        need_redraw_down = 0;
      }
	
      /* Redraw from current cursor position */
      update_from_pos = buf.cursor_pos;
    }

    need_status_update = 1;
}

/* Regular enter now inserts CR ($0D) */
do_cr_enter()
{
    /* (1) Insert the CR character */
    ensure_gap_at_cursor();
    if (!gap_has_space()) return;
    
    add_undo(buf.cursor_pos, 0, CR);  /* Insert CR */
    *(text_ptr + buf.gap_start) = CR;  /* Insert CR */
    buf.gap_start++;
    buf.cursor_pos++;
    buf.text_length++;
    set_dirty(1);
    total_logical_lines++;  /* Track total lines */
    
    /* Update logical line tracking */

        buf.ccurs_ln++;

    
    /* (2) Clear to end of current line */
    write_block(1, CLEAR_EOL, 1);
    
    /* (3) Advance cursor to next row and start drawing */
    log_row++;
    cursor_col = 0;
    write_pos(cursor_col, PHYS_ROW(log_row));
     
    /* Draw remaining text from cursor position */
    if (buf.cursor_pos < buf.text_length) {
        need_redraw_down = 1;
        update_from_pos = buf.cursor_pos;
    }
    
    need_status_update = 1;  /* Update status bar for line count change */
}

/* Function to insert LF ($0A) for Shift+Enter */
do_lf_enter()
{
    /* (1) Insert the LF character */
    ensure_gap_at_cursor();
    if (!gap_has_space()) return;
    
    add_undo(buf.cursor_pos, 0, LF);  /* Insert LF */
    *(text_ptr + buf.gap_start) = LF;  /* Insert LF */
    buf.gap_start++;
    buf.cursor_pos++;
    buf.text_length++;
    set_dirty(1);
    total_logical_lines++;  /* Track total lines */
    
    /* Update logical line tracking */

        buf.ccurs_ln++;

    
    /* (2) Clear to end of current line */
    putchar(0x04);
    
    /* (3) Advance cursor to next row and start drawing */
    log_row++;
    cursor_col = 0;
    putchar(0x02);
    putchar(cursor_col + 0x20);
    putchar(PHYS_ROW(log_row) + 0x20);
    
    /* Draw remaining text from cursor position */
    if (buf.cursor_pos < buf.text_length) {
        need_redraw_down = 1;
        update_from_pos = buf.cursor_pos;
    }
    
    need_status_update = 1;  /* Update status bar for line count change */
}

/* Ensure cursor is visible on screen, scroll if necessary */
ensure_vis()
{
    int screen_pos, vis_lines, screen_height, is_visible;
    int i, j;
    
    /* Check if cursor position is visible on screen */
    screen_pos = buf.topscr_pos;
    vis_lines = 0;
    screen_height = eff_rows;
    is_visible = 0;
    
    /* Walk through visible positions counting visual lines */
    while (screen_pos <= buf.cursor_pos && vis_lines < screen_height) {
        if (screen_pos == buf.cursor_pos) {
            is_visible = 1;
            break;
        }
        screen_pos = visln_next(screen_pos);
        vis_lines++;
        if (screen_pos <= buf.cursor_pos && screen_pos >= buf.text_length) break;
    }
    
    /* If cursor is off-screen, scroll to show it */
    if (!is_visible) {
        /* Center the cursor line on screen */
        buf.topscr_pos = visln_sta(buf.cursor_pos);
        /* Move back a few visual lines for context */
        for (j = 0; j < 5 && buf.topscr_pos > 0; j++) {
            buf.topscr_pos = visln_pre(buf.topscr_pos);
        }
        need_full_redraw = 1;
    }
}

goto_ln(line)
int line;
{
    int i, j, current_line;
    int screen_pos, vis_lines, screen_height, is_visible;
    
    current_line = 0;
    for (i = 0; i < buf.text_length && current_line < line; i++) {
        if (IS_LINE_END(gap_char_at(i))) {
            current_line = current_line + 1;
        }
    }
    
    set_curs(i);
    
    /* Report actual line reached (may be less than requested) */
    if (current_line < line) {
        sprintf(status_msg, "Jumped to line %d (last line, requested %d)", current_line + 1, line + 1);
    } else {
        sprintf(status_msg, "Jumped to line %d", line + 1);
    }
    
    /* Check if target position is visible on screen */
    screen_pos = buf.topscr_pos;
    vis_lines = 0;
    screen_height = eff_rows;  /* Use effective rows (accounts for double-spacing) */
    is_visible = 0;
    
    /* Walk through visible positions counting visual lines */
    while (screen_pos <= i && vis_lines < screen_height) {
        if (screen_pos == i) {
            is_visible = 1;
            break;
        }
        screen_pos = visln_next(screen_pos);
        vis_lines++;
        if (screen_pos <= i && screen_pos >= buf.text_length) break;
    }
    
    /* If target line is off-screen, scroll to show it */
    if (!is_visible) {
        /* Center the target line on screen */
        buf.topscr_pos = line_sta(i);
        /* Move back a few visual lines for context */
        for (j = 0; j < 5 && buf.topscr_pos > 0; j++) {
            buf.topscr_pos = visln_pre(buf.topscr_pos);
        }
    }
}

clear_eol(from_col, row)
int from_col, row;
{
    putchar(0x04);
}

add_undo(pos, action, ch)
int pos, action;
char ch;
{
    int i;
    
    if (buf.undo_count < MAX_UNDO) {
        /* Buffer not full - add normally */
        buf.undo_buf[buf.undo_count].pos = pos;
        buf.undo_buf[buf.undo_count].action = action;
        buf.undo_buf[buf.undo_count].ch = ch;
        buf.undo_count++;
    } else {
        /* Buffer full - shift everything left to make room */
        /* This discards the oldest operation and keeps the most recent */
        for (i = 0; i < MAX_UNDO - 1; i++) {
            /* Copy struct members individually (OS-9 C compatibility) */
            buf.undo_buf[i].pos = buf.undo_buf[i + 1].pos;
            buf.undo_buf[i].action = buf.undo_buf[i + 1].action;
            buf.undo_buf[i].ch = buf.undo_buf[i + 1].ch;
        }
        /* Add new entry at end */
        buf.undo_buf[MAX_UNDO - 1].pos = pos;
        buf.undo_buf[MAX_UNDO - 1].action = action;
        buf.undo_buf[MAX_UNDO - 1].ch = ch;
        /* undo_count stays at MAX_UNDO */
    }
}

do_undo()
{
    struct UndoEntry *entry;
    int i;
    
    if (buf.undo_count <= 0) {
        strcpy(status_msg, "Nothing to undo");
        need_status_update = 1;
        return;
    }
    
    buf.undo_count = buf.undo_count - 1;
    entry = &buf.undo_buf[buf.undo_count];
    
    if (entry->action == 0) {
        /* Undo insert: delete the character that was inserted */
        /* Position cursor after the inserted character, then delete backward */
        set_curs(entry->pos + 1);
        ensure_gap_at_cursor();
        if (buf.cursor_pos > 0) {
            buf.gap_start--;  /* Delete by moving gap start backward (like backspace) */
            buf.cursor_pos--;
            buf.text_length--;
        }
    } else {
        /* Insert character (gap buffer style) */
        ensure_gap_at_cursor();
        if (gap_has_space()) {
            *(text_ptr + buf.gap_start) = entry->ch;
            buf.gap_start++;
            buf.text_length++;
            set_curs(entry->pos + 1);
        }
    }
    
    set_dirty(1);
    strcpy(status_msg, "Undone");
    temp_message_active = 1;
    need_status_update = 1;
    recount_total_lines();  /* Recalculate after undo */
    update_from_pos = entry->pos;
    need_minimal_update = 1;
}

/* Unified display function - replaces fast_show, fast_line, fast_from_pos
 * from_pos: starting buffer position
 * stop_type: -1=end of line, -2=bottom of screen, else stop at position
 * row: starting row on screen
 * col: starting column on screen
 */
draw_range(from_pos, stop_type, row, col)
int from_pos, stop_type, row, col;
{
    int i, chunk_start, stop_pos;
    char ch;
    int in_reverse;
    int need_wrap;  /* Add flag for wrap handling */
    int just_wrapped;
    
    in_reverse = 0;
    need_wrap = 0;
    
    /* Defensive checks */
    if (from_pos < 0) from_pos = 0;

    if (from_pos >= buf.text_length) {
      /* At or past end of buffer - clear the screen from this position */
      if (row < status_row) {
        write_pos(col, row);
        write_block(1, CLEAR_EOL, 1);
      }
        
      /* Clear remaining lines if drawing full screen */
      if (stop_type == -2 && row < status_row - 1) {
	row++;
	while (row < status_row) {
	  write_pos(0, row);
	  write_block(1, ERASE_LINE, 1);
	  row++;
	}
      }
      return;
    }
    
    
    /* Determine stopping position */
    if (stop_type == -1) {
        stop_pos = line_end(from_pos);
    } else if (stop_type == -2) {
        stop_pos = buf.text_length;
    } else {
        stop_pos = stop_type;
    }
    
    /* Validate bounds */
    if (stop_pos > buf.text_length) stop_pos = buf.text_length;
    if (from_pos >= stop_pos) return;
    
    /* Position cursor at starting location */
    write_pos(col, row);
    
    /* Check initial selection state */
    if (buf.selecting && from_pos >= buf.select_start && from_pos < buf.select_end) {
        write_block(1, REV_ON, 2);
        in_reverse = 1;
    }
    
    /* Main drawing loop with chunking */
    i = from_pos;
    while (i < stop_pos && row < status_row) {
        
        /* Check for selection boundary at current position */
        if (buf.selecting) {
            if (i == buf.select_start && !in_reverse) {
                write_block(1, REV_ON, 2);
                in_reverse = 1;
            } else if (i == buf.select_end && in_reverse) {
                write_block(1, REV_OFF, 2);
                in_reverse = 0;
            }
        }

	just_wrapped = 0;
        /* Handle pending wrap from previous iteration */
        if (need_wrap) {
            row++;
            if (dbl_space) {
                /* Erase the spacing row */
                if (row < status_row) {
                    write_pos(0, row);
                    write_block(1, ERASE_LINE, 1);
                }
                row++;  /* Skip to actual text row */
            }
            if (row >= status_row) break;
            write_pos(0, row);
            col = 0;
            need_wrap = 0;
	    just_wrapped = 1;
        }
        
        /* Build chunk of regular characters */
        chunk_start = i;
        while (i < stop_pos && row < status_row) {
            ch = gap_char_at(i);
            
            /* Stop chunk at special characters */
            if (IS_LINE_END(ch) || ch == 9) break;
            
            /* Stop chunk at NEXT selection boundary */
            if (buf.selecting) {
                if ((i + 1) == buf.select_start || (i + 1) == buf.select_end) {
                    /* Include current char, stop before boundary */
                    col++;  /* Count this character */
                    i++;
                    if (col >= screen_cols) need_wrap = 1;
                    break;
                }
            }
            
            /* Regular printable character */
            if (ch >= 32 && ch < 127) {
	      /* Check if we're about to hit column 80 */
	      if (col == screen_cols - 1) {
		/* Output chunk up to but not including this character */
		if (i > chunk_start) {
		  write_gap_block(1, chunk_start, i - chunk_start);
		}
            
		/* Output the character at column 79 */
		putchar(ch);
            
		/* Manually position to next line BEFORE terminal auto-wraps */
		row++;
		if (dbl_space) {
		    /* Erase the spacing row */
		    if (row < status_row) {
		        write_pos(0, row);
		        write_block(1, ERASE_LINE, 1);
		    }
		    row++;  /* Skip to actual text row */
		}
		if (row >= status_row) {
		  i++;
		  break;
		}
		write_pos(0, row);
		col = 0;
		i++;
            
		/* Start new chunk from next character */
		chunk_start = i;
	      } else {
		col++;
		i++;
	      }
            } else {
	      /* Non-printable - treat as single char */
	      col++;
	      i++;
	      if (col >= screen_cols) need_wrap = 1;
	      break;
            }
        }
        
        /* Output the chunk if we have characters */
        if (i > chunk_start) {
            write_gap_block(1, chunk_start, i - chunk_start);
        }
        
        /* Process special character if we stopped at one (not wrap) */
        if (i < stop_pos && row < status_row && !need_wrap) {
            ch = gap_char_at(i);
            
            if (IS_LINE_END(ch)) {
	      /* Make sure reverse video is off before clearing line */
	      if (in_reverse) {
		write_block(1, REV_OFF, 2);
		in_reverse = 0;
	      }
	      
	      write_block(1, CLEAR_EOL, 1);
                
	      /* For line-only mode, stop here */
	      if (stop_type == -1) break;
                
	      /* Move to next line */
	      row++;
	      if (dbl_space) {
	          /* Erase the spacing row */
	          if (row < status_row) {
	              write_pos(0, row);
	              write_block(1, ERASE_LINE, 1);
	          }
	          row++;  /* Skip to actual text row */
	      }
	      if (row >= status_row) break;
	      write_pos(0, row);
	      col = 0;
	      i++;

	      /* Check if we need to turn reverse back on for next line */
	      if (buf.selecting && i >= buf.select_start && i < buf.select_end) {
		write_block(1, REV_ON, 2);
		in_reverse = 1;
	      }
                
            } else if (ch == 9) {
                /* Tab character */
                int target_col;
                target_col = NEXT_TAB(col);
                
                while (col < target_col && col < screen_cols) {
                    putchar(' ');
                    col++;
                }
                
                if (col >= screen_cols) {
                    need_wrap = 1;
                }
                i++;
            }
        }
    }
    
    /* Ensure selection state is off when done */
    if (in_reverse) {
        write_block(1, REV_OFF, 2);
    }
    
    /* Clear to end of current line */
    if ((col > 0 || i >= buf.text_length - 1) && row < status_row) {
        write_block(1, CLEAR_EOL, 1);
    }
    
    /* If we reached end of buffer, clear remaining lines on screen */
    /* This handles un-wrapping of the last line */
    if (i >= buf.text_length && row < status_row - 1) {
        row++;
        while (row < status_row) {
            write_pos(0, row);
            write_block(1, ERASE_LINE, 1);
            row++;
        }
    }
    
    /* For full screen mode, also clear remaining lines */
    if (stop_type == -2 && row < status_row - 1) {
        row++;
        while (row < status_row) {
            write_pos(0, row);
            write_block(1, ERASE_LINE, 1);
            row++;
        }
    }
}

/* Replace fast_show() */
fast_show()
{
    write_block(1, HIDE_CURSOR, 2);
    draw_range(buf.topscr_pos, -2, text_start_row, 0);
    write_block(1, SHOW_CURSOR, 2);
}

/* Replace fast_line() */  
fast_line(from_pos)
int from_pos;
{
    int row, col, i, vis_start;
    char ch;
    
    /* Find the start of the visual line containing from_pos */
    vis_start = visln_sta(from_pos);
    
    /* Find the screen row where this visual line starts */
    /* Count lines from top of screen to vis_start */
    i = get_top_pos();
    row = text_start_row;
    col = 0;
    
    /* Scan to find screen position of vis_start */
    while (i < vis_start && i < buf.text_length) {
        ch = gap_char_at(i);
        if (IS_LINE_END(ch)) {
            row++;
            if (dbl_space) row++;  /* Add spacing between lines */
            col = 0;
        } else if (ch == 9) {
            do {
                col++;
                if (col >= screen_cols) {
                    row++;
                    if (dbl_space) row++;  /* Add spacing for wrapped lines */
                    col = 0;
                    break;
                }
            } while (TAB_MOD(col) != 0);
        } else {
            col++;
            if (col >= screen_cols) {
                row++;
                if (dbl_space) row++;  /* Add spacing for wrapped lines */
                col = 0;
            }
        }
        i++;
    }
    
    /* Now row is correct, and col should be 0 (start of visual line) */
    /* Continue scanning from vis_start to from_pos to get column */
    col = 0;
    while (i < from_pos && i < buf.text_length) {
        ch = gap_char_at(i);
        if (ch == 9) {
            col = NEXT_TAB(col);
        } else {
            col++;
        }
        i++;
    }
    
    /* Now row, col are the screen coordinates for from_pos */
    write_block(1, HIDE_CURSOR, 2);
    draw_range(from_pos, -1, row, col);
    
    /* Recalculate cursor position after drawing (handles tabs, etc.) */
    fast_curs();
    
    write_block(1, SHOW_CURSOR, 2);
}

/* Replace fast_from_pos() */
fast_from_pos(from_pos)  
int from_pos;
{
    int row, col, i, vis_start;
    char ch;
    
    /* Find the start of the visual line containing from_pos */
    vis_start = visln_sta(from_pos);
    
    /* Find the screen row where this visual line starts */
    i = get_top_pos();
    row = text_start_row;
    col = 0;
    
    /* Scan to find screen position of vis_start */
    while (i < vis_start && i < buf.text_length) {
        ch = gap_char_at(i);
        if (IS_LINE_END(ch)) {
            row++;
            if (dbl_space) row++;  /* Add spacing between lines */
            col = 0;
        } else if (ch == 9) {
            do {
                col++;
                if (col >= screen_cols) {
                    row++;
                    if (dbl_space) row++;  /* Add spacing for wrapped lines */
                    col = 0;
                    break;
                }
            } while (TAB_MOD(col) != 0);
        } else {
            col++;
            if (col >= screen_cols) {
                row++;
                if (dbl_space) row++;  /* Add spacing for wrapped lines */
                col = 0;
            }
        }
        i++;
    }
    
    /* Now row is correct, calculate column from vis_start to from_pos */
    col = 0;
    while (i < from_pos && i < buf.text_length) {
        ch = gap_char_at(i);
        if (ch == 9) {
            col = NEXT_TAB(col);
        } else {
            col++;
        }
        i++;
    }
    
    write_block(1, HIDE_CURSOR, 2);
    draw_range(from_pos, -2, row, col);
    fast_curs();
    write_block(1, SHOW_CURSOR, 2);
}

/* Fast cursor positioning - just count to cursor and track position */
fast_curs()
{
    int start_pos, i, row, col;
    char ch;
    
    /* Use cached screen start position */
    start_pos = get_top_pos();
    
    /* Simulate display from start to cursor to find logical row */
    row = 0;  /* Start at logical row 0 */
    col = 0;
    
    for (i = start_pos; i < buf.cursor_pos && i < buf.text_length; i++) {
        ch = gap_char_at(i);
        
        if (IS_LINE_END(ch)) {
            row++;
            col = 0;
        } else if (ch == 9) {
            do {
                col++;
                if (col >= screen_cols) {
                    row++;
                    col = 0;
                    break;
                }
            } while (TAB_MOD(col) != 0);
        } else {
            col++;
            if (col >= screen_cols) {
                row++;
                col = 0;
            }
        }
    }

    /* Check if calculated position is actually visible on screen */
    if (row < 0 || row >= eff_rows) {
        /* Cursor calculated to be off-screen - don't position */
        return;
    }

    /* UPDATE GLOBAL VARIABLES */
    log_row = row;
    cursor_col = col;
    cur_end_col = calc_end_col();

    /* Position cursor using calculated coordinates */
    putchar(0x02);
    putchar(col + 0x20);
    putchar(PHYS_ROW(row) + 0x20);
}

/* Redraw just the title bar - used when dirty flag changes */
draw_title()
{
    int len1, lenf, i, j;
    char linebuffer[81];
    char *line;
    
    /* Hide cursor and position to row 0, column 0 */
    write_block(1, HIDE_CURSOR, 2);
    putchar(0x02);  /* Position cursor command */
    putchar(0 + 0x20);  /* Column 0 */
    putchar(0 + 0x20);  /* Row 0 */
    
    line = linebuffer;
    lenf = strlen(fname_ptr);
    len1 = (screen_cols - lenf) / 2 - 2;
    
    for (i = 0; i < len1; i++) {
        line[i] = CH_TOPBL;
    }
    
    line[i] = CH_TOPLTC;
    i++;
    line[i] = 32;
    i++;
    
    for (j = 0; j < lenf; j++) {
        line[i] = fname_ptr[j];
        i++;
    }
    
    /* Add asterisk if file is modified */
    if (buf.dirty) {
        line[i] = '*';
        i++;
    }
    
    line[i] = 32;
    i++;
    line[i] = CH_TOPRTC;
    i++;
    
    for (; i < 80; i++) {
        line[i] = CH_TOPBL;
    }
    
    /* Write title line */
    write_block(1, line, 80);
    
    /* Restore cursor */
    write_block(1, SHOW_CURSOR, 2);
}

/* Set dirty flag and update title bar if it changed */
set_dirty(new_value)
int new_value;
{
    if (buf.dirty != new_value) {
        buf.dirty = new_value;
        need_title_update = 1;
    }
}

/* Fast setup screen */
fast_scr()
{

   int len1;
    int len2;
    int lenf;
    char linebuffer[81];
    char *line;
    int i;
    int j;
      
  /* Hide cursor */
    putchar(0x05);
    putchar(0x20);  
    putchar(0x01);  /* HOME cursor */

    line = linebuffer;
    lenf = strlen(fname_ptr);
    len1 = (screen_cols - lenf) / 2 - 2;
    
 
    for (i=0; i<len1; i++){
      line[i]= CH_TOPBL;
    }

    line[i] = CH_TOPLTC;
    i++;
    line[i] = 32;
    i++;

    for (j=0; j<lenf; j++){
      line[i] = fname_ptr[j];
      i++;
    }

    /* Add asterisk if file is modified */
    if (buf.dirty) {
      line[i] = '*';
      i++;
    }

    line[i] = 32;
    i++;
    line[i] = CH_TOPRTC;
    i++;

    for (; i<80; i++){
      line[i] = CH_TOPBL;
    }
    
    /* Header line */

    write_block(1,line,80);
      /*  printf("File: %-20s %s Pos: %d/%d ]", 
           fname_ptr, 
           buf.dirty ? "[*]" : "   ",
           buf.cursor_pos, buf.text_length);*/
           
    
    /* Separator line */
/*    putchar(0x02);
    putchar(0 + 0x20);
    putchar(2 + 0x20);
    printf("----------------------------------------"); */
    
    fast_show();
    draw_stat();
    fast_curs();
    /*   putchar(0x05);
	 putchar(0x21);*/ 
}

/* Fast minimal update - just redraw from changed position */
fast_upd(from_pos)
int from_pos;
{
    /* Use efficient single line update with caching */
    fast_line(from_pos);
}

/* Fast single character or simple line update - no selection handling needed */
fast_char_upd(from_pos)
int from_pos;
{
    int row, col, i, chunk_start;
    char ch;
    
    /* Calculate screen position */
    col = cursor_col;
    row = log_row;
    
    if (from_pos < buf.cursor_pos && (buf.cursor_pos - from_pos) == 1) {
        col--;
        if (col < 0) {
            row--;
            col = screen_cols - 1;
        }
    }
    
    write_block(1, HIDE_CURSOR, 2);
    write_pos(col, PHYS_ROW(row));  /* Convert to physical row */
    
    /* Build chunks of regular characters and output them */
    i = from_pos;
    while (i < buf.text_length && row < eff_rows) {
        ch = gap_char_at(i);
        
        /* Stop at line ending */
        if (IS_LINE_END(ch)) break;
        
        /* Handle tab - output previous chunk first, then spaces */
        if (ch == 9) {
            int target_col = NEXT_TAB(col);
            while (col < target_col) {
                putchar(' ');
                col++;
                if (col >= screen_cols) {
                    /* Wrap to next visual line */
                    row++;
                    col = 0;
                    if (row >= eff_rows) break;
                    write_pos(col, PHYS_ROW(row));  /* Convert to physical row */
                }
            }
            i++;
        } else if (ch >= 32 && ch < 127) {
            /* Build chunk of regular characters */
            chunk_start = i;
            while (i < buf.text_length) {
                ch = gap_char_at(i);
                
                /* Stop chunk at special characters */
                if (IS_LINE_END(ch) || ch == 9 || ch < 32 || ch >= 127) break;
                
                /* Check if we'd wrap */
                if (col >= screen_cols) {
                    /* Output chunk before wrapping */
                    if (i > chunk_start) {
                        write_gap_block(1, chunk_start, i - chunk_start);
                    }
                    /* Wrap to next visual line */
                    row++;
                    col = 0;
                    if (row >= eff_rows) break;
                    write_pos(col, PHYS_ROW(row));  /* Convert to physical row */
                    chunk_start = i;  /* Start new chunk */
                }
                
                col++;
                i++;
            }
            
            /* Output remaining chunk */
            if (i > chunk_start && row < status_row) {
                write_gap_block(1, chunk_start, i - chunk_start);
            }
        } else {
            /* Non-printable - skip */
            col++;
            if (col >= screen_cols) {
                row++;
                col = 0;
                if (row >= eff_rows) break;
                write_pos(col, PHYS_ROW(row));  /* Convert to physical row */
            }
            i++;
        }
    }
    
    /* Clear rest of current line */
    if (row < status_row) {
        write_block(1, CLEAR_EOL, 1);
    }
    
    write_pos(cursor_col, PHYS_ROW(log_row));
    write_block(1, SHOW_CURSOR, 2);
}

/* Enhanced upd_fast() with auto-scroll coordination */
upd_fast()
{
    static int update_in_progress = 0;
    int scroll_occurred = 0;
    int lines_to_scroll;
    int new_top_pos;
    
    /* Don't update display while in help mode */
    if (in_help_mode) return;
    
    if (update_in_progress) return;
    update_in_progress = 1;
    
    /* Simple visual line scrolling using buffer positions */
    if (log_row < 0) {
        /* Cursor above screen - scroll up by visual lines */
        lines_to_scroll = -log_row;
        new_top_pos = get_top_pos();
        
        /* Use visln_pre() to scroll up the exact number of visual lines */
        while (lines_to_scroll > 0 && new_top_pos > 0) {
            new_top_pos = visln_pre(new_top_pos);
            lines_to_scroll--;
        }
        
        buf.topscr_pos = new_top_pos;
        need_full_redraw = 1;
        scroll_occurred = 1;
        
        need_status_update = 1;
        
    } else if (log_row >= eff_rows) {
        /* Cursor below screen - scroll down by visual lines */
        lines_to_scroll = log_row - eff_rows + 1;
        new_top_pos = get_top_pos();
        
        /* Use visln_next() to scroll down the exact number of visual lines */
        while (lines_to_scroll > 0 && new_top_pos < buf.text_length) {
            new_top_pos = visln_next(new_top_pos);
            lines_to_scroll--;
        }
        
         buf.topscr_pos = new_top_pos;
        need_full_redraw = 1;
        scroll_occurred = 1;
        
        need_status_update = 1;
    }
    
    /* Existing update logic - but scroll check may have set need_full_redraw */
   if (scroll_occurred) {
        /* If cursor ended up beyond visible area, force it to bottom of text area */
        if (log_row >= eff_rows) {
            log_row = eff_rows - 1;  /* Bottom row of text area */
        }
        
        /* Position cursor at the corrected coordinates */
        putchar(0x02);
        putchar(cursor_col + 0x20);
        putchar(PHYS_ROW(log_row) + 0x20);
    }
    
    /* Existing update logic - but scroll check may have set need_full_redraw */
    if (need_full_redraw) {
        fast_scr();
        need_full_redraw = 0;
        need_status_update = 0;
        need_minimal_update = 0;
        update_from_pos = -1;
        last_cursor_pos = buf.cursor_pos;
     } else if (need_char_update && update_from_pos >= 0) {
        /* NEW: Fast character update path */
        fast_char_upd(update_from_pos);
        need_char_update = 0;
        update_from_pos = -1;
    } else if (need_redraw_down && update_from_pos >= 0) {
        fast_from_pos(update_from_pos);
        need_redraw_down = 0;
        update_from_pos = -1;	
    } else if (need_minimal_update && update_from_pos >= 0) {
        fast_upd(update_from_pos);
        need_minimal_update = 0;
        update_from_pos = -1;
    } else if (buf.cursor_pos != last_cursor_pos) {
        fast_curs();
        last_cursor_pos = buf.cursor_pos;
    }
    
    if (need_status_update) {
        draw_stat();
        need_status_update = 0;
        fast_curs();
    }

    if (need_title_update) {
        draw_title();
        need_title_update = 0;
        fast_curs();  /* Restore cursor after title draw */
    }
    
    
    update_in_progress = 0;
}




/* Find start of next visual line from given buffer position */
visln_next(start_pos)
int start_pos;
{
  int pos = visln_sta(start_pos);
  int col = 0;
    
    /* Advance until we reach end of current visual line */
    while (pos < buf.text_length) {
        char ch = gap_char_at(pos);
        
        if (IS_LINE_END(ch)) {
            /* Hit newline - next char is start of next visual line */
            return pos + 1;
        }
        
        if (ch == 9) {
            /* Tab - advance to next tab stop */
            col = NEXT_TAB(col);
        } else {
            col++;
        }
        
        if (col >= screen_cols) {
            /* Hit column 80 - next char is start of next visual line */
            return pos + 1;
        }
        
        pos++;
    }
    
    return buf.text_length;  /* End of buffer */
}

/* Find start of previous visual line from given buffer position */
visln_pre(start_pos)
int start_pos;
{
    int pos, prev_pos, newline_count, col, scan_pos;

    /* if start pos < top of screen then return 0 */
    if (start_pos <= 0) return 0;
    
    /* Step 1: Go back to find starting point for search */
    /* Go back 2 newlines or to buffer start from start_pos */
    pos = start_pos;
    newline_count = 0;
    while (pos > 0 && newline_count < 2) {
        pos--;
        if (IS_LINE_END(gap_char_at(pos))) {
            newline_count++;
        }
    }
    
    /* Step 2: Scan forward tracking visual line starts */
    /* Skip past any newline at current position if we are 2 lines back to start scanning */
    /* Skip this if only 1 or 0 lines back */
    if (pos < buf.text_length && IS_LINE_END(gap_char_at(pos)) && newline_count > 1) {
        pos++;
    }
    
    prev_pos = pos;  /* Remember the first visual line start */
    scan_pos = pos;
    col = 0;
    
    while (scan_pos < start_pos) {
        char ch = gap_char_at(scan_pos);
        
        if (IS_LINE_END(ch)) {
            /* Newline - next position starts new visual line */
            prev_pos = pos;           /* Remember previous visual line start */
            pos = scan_pos + 1;       /* This becomes current visual line start */
            col = 0;
        } else {
            if (ch == 9) {
                col = NEXT_TAB(col);
            } else {
                col++;
            }
            
            if (col >= screen_cols) {
                /* Visual line wrap - next position starts new visual line */
                prev_pos = pos;           /* Remember previous visual line start */
                pos = scan_pos + 1;       /* This becomes current visual line start */
                col = 0;
            }
        }
        
        scan_pos++;
    }
    
    return prev_pos;  /* Return the previous visual line start */
}

visln_sta(pos)
int pos;
{
    int ln_start, scan_pos, col;
    int last_vis_start;
    char ch;
    
    if (pos <= 0) return 0;
    
    /* Start from logical line beginning */
    ln_start = line_sta(pos);
    last_vis_start = ln_start;
    scan_pos = ln_start;
    col = 0;
    
    /* Scan forward tracking visual line starts until we reach pos */
    while (scan_pos < pos && scan_pos < buf.text_length) {
        ch = gap_char_at(scan_pos);
        
        if (IS_LINE_END(ch)) {
            last_vis_start = scan_pos + 1;
            scan_pos++;
            col = 0;
            continue;
        }
        
        if (ch == 9) {
            col = NEXT_TAB(col);
        } else {
            col++;
        }
        
        if (col >= screen_cols) {
            last_vis_start = scan_pos + 1;
            col = 0;
        }
        
        scan_pos++;
    }
    
    return last_vis_start;
}

/* Calculate visual column of end of current line from cursor position */

/* Calculate visual width of a tab at given column position */
calc_tab_width(col)
int col;
{
    return tab_wdth - TAB_MOD(col);
}

calc_end_col()
{
    int pos, col;
    char ch;
    
    pos = buf.cursor_pos;
    col = cursor_col;
    
    while (pos < buf.text_length) {
        ch = gap_char_at(pos);
        if (IS_LINE_END(ch)) break;
        
        if (ch == 9) {
            col = NEXT_TAB(col);
        } else {
            col++;
        }
        
        /* Wrap column at screen boundary */
        if (col >= screen_cols) {
            col = col - screen_cols;
        }
        
        pos++;
    }
    
    return col;
}
