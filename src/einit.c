/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2021 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: May 2021 */


/* This file contains initializing code, including the main program, which is
the entry point to NE. It also contains termination code. */

#define _POSIX_SOURCE     /* Needed to get fileno(), apparently */

#include "ehdr.h"
#include "cmdhdr.h"


static uschar *arg_from_buffer;
static uschar *arg_to_buffer;

static jmp_buf rdline_env;



/*************************************************
*            Interrupt handler                   *
*************************************************/

/* We simply set a flag. Resetting the trap happens later when the signal is
noticed. This allows two interrupts to kill a looping NE. */

static void sigint_handler(int sig)
{
(void)sig;
main_escape_pressed = TRUE;
}



/*************************************************
*        Interrupt handler during fgets          *
*************************************************/

static void fgets_sigint_handler(int sig)
{
(void)sig;
main_escape_pressed = TRUE;
longjmp(rdline_env, 1);
}



/*************************************************
*           Check for interruption               *
*************************************************/

/* The call to sys_checkinterrupt() allows for checking in addition to the
normal signal handler. Used in Unix to check for ctrl/c explicitly in raw input
mode. To help avoid obeying expensive system calls too often, a count of calls
since the last command line was read is maintained, and the type of activity in
progress at the time of the call is also passed on. */

BOOL main_interrupted(int type)
{
main_cicount++;
sys_checkinterrupt(type);
if (main_escape_pressed)
  {
  main_escape_pressed = FALSE;
  signal(SIGINT, sigint_handler);
  if (main_attn || main_oneattn)
    {
    main_oneattn = FALSE;
    error_moan(23);
    return TRUE;
    }
  else
    {
    main_oneattn = TRUE;
    return FALSE;
    }
  }
else return FALSE;
}


/*************************************************
*               Flush interruption               *
*************************************************/

void main_flush_interrupt(void)
{
if (main_escape_pressed)
  {
  main_escape_pressed = FALSE;
  signal(SIGINT, sigint_handler);
  }
}



/*************************************************
*            Initialize a new buffer             *
*************************************************/

void init_buffer(bufferstr *buffer, int n, uschar *name, uschar *alias,
  FILE *ffid, int rmargin)
{
usint i;
for (i = 0; i < sizeof(bufferstr)/sizeof(int); i++) ((int *)buffer)[i] = 0;
buffer->binoffset = 0;
buffer->bufferno = n;
buffer->changed = buffer->saved = FALSE;
buffer->imax = 1;
buffer->imin = 0;
buffer->rmargin = rmargin;
buffer->filename = name;
buffer->filealias = alias;
buffer->readonly = main_readonly;

buffer->backlist = store_Xget(back_size * sizeof(backstr));
buffer->backlist[0].line = NULL;
buffer->backnext = 0;
buffer->backtop = 0;

/* Set up first line in the buffer */

if (ffid == NULL)
  {
  buffer->bottom = buffer->top = store_getlbuff(0);
  buffer->top->flags |= lf_eof;
  buffer->top->key = buffer->linecount = 1;
  }
else
  {
  buffer->bottom = buffer->top = file_nextline(&ffid, &buffer->binoffset);
  buffer->top->key = buffer->linecount = 1;

  while ((buffer->bottom->flags & lf_eof) == 0)
    {
    linestr *last = buffer->bottom;
    buffer->bottom = file_nextline(&ffid, &buffer->binoffset);
    buffer->bottom->key = buffer->imax += 1;
    last->next = buffer->bottom;
    buffer->bottom->prev = last;
    buffer->linecount += 1;
    }
  }

buffer->from_fid = ffid;
buffer->current = buffer->top;
}



/************************************************
*          Select Editing Buffer                *
************************************************/

void init_selectbuffer(bufferstr *buffer, BOOL changeflag)
{

/* First of all, salt away current parameters */

if (currentbuffer != NULL)
  {
  currentbuffer->backlist = main_backlist;
  currentbuffer->backnext = main_backnext;
  currentbuffer->backtop = main_backtop;
  currentbuffer->changed = main_filechanged;
  currentbuffer->from_fid = from_fid;
  currentbuffer->imax = main_imax;
  currentbuffer->imin = main_imin;
  currentbuffer->linecount = main_linecount;
  currentbuffer->readonly = main_readonly;
  currentbuffer->row = cursor_row;
  currentbuffer->col = cursor_col;
  currentbuffer->offset = cursor_offset;
  currentbuffer->rmargin = main_rmargin;
  currentbuffer->top = main_top;
  currentbuffer->bottom = main_bottom;
  currentbuffer->current = main_current;
  currentbuffer->filename = main_filename;
  currentbuffer->filealias = main_filealias;

  currentbuffer->marktype = mark_type;
  currentbuffer->markline = mark_line;
  currentbuffer->markline_global = mark_line_global;
  currentbuffer->markcol = mark_col;
  currentbuffer->markcol = mark_col_global;

  if (main_screenOK && !changeflag)
    currentbuffer->scrntop = window_vector[0];
  }

/* Now set parameters from saved block */

main_backlist = buffer->backlist;
main_backnext = buffer->backnext;
main_backtop = buffer->backtop;
main_filechanged = buffer->changed;
from_fid = buffer->from_fid;
main_imax = buffer->imax;
main_imin = buffer->imin;
main_linecount = buffer->linecount;
cursor_row = buffer->row;
cursor_col = buffer->col;
cursor_offset = buffer->offset;
main_readonly = buffer->readonly;
main_rmargin = buffer->rmargin;
main_top = buffer->top;
main_bottom = buffer->bottom;
main_current = buffer->current;
main_filename = buffer->filename;
main_filealias = buffer->filealias;

mark_type = buffer->marktype;
mark_line = buffer->markline;
mark_line_global = buffer->markline_global;
mark_col = buffer->markcol;
mark_col_global = buffer->markcol_global;

cursor_max = cursor_offset + window_width;
currentbuffer = buffer;

if (main_screenOK)
  {
  screen_forcecls = TRUE;
  scrn_hint(sh_topline, 0, buffer->scrntop);
  }
}





/*************************************************
*               Start up a file                  *
*************************************************/

/* This function is called when we start up the first window. It expects any
previous stuff to have been tidied away already. In some modes of operation,
the input file has already been opened (to test for its existence). Hence the
alternative interfaces: either the fid can be passed with the name NULL, or a
name can be passed. */

BOOL init_init(FILE *fid, uschar *fromname, uschar *toname)
{
if (fid == NULL && fromname != NULL && fromname[0] != 0)
  {
  if (Ustrcmp(fromname, "-") == 0)
    {
    from_fid = stdin;
    main_interactive = main_verify = FALSE;
    if (cmdin_fid == stdin) cmdin_fid = NULL;
    if (msgs_fid == stdout) msgs_fid = stderr;
    }
  else
    {
    from_fid = sys_fopen(fromname, US"r");
    if (from_fid == NULL)
      {
      error_moan(5, fromname, "reading", strerror(errno));
      return FALSE;
      }
    }
  }
else from_fid = fid;

main_initialized = FALSE;    /* errors now are fatal */

/* Now initialize the first buffer */

main_bufferchain = store_Xget(sizeof(bufferstr));
init_buffer(main_bufferchain, 0, store_copystring(toname),
  store_copystring(toname), from_fid, main_rmargin);
main_nextbufferno = 1;
init_selectbuffer(main_bufferchain, FALSE);

main_filechanged = TRUE;
if ((fromname == NULL && toname == NULL) ||
    (fromname != NULL && toname != NULL && Ustrcmp(fromname, toname) == 0 &&
      Ustrcmp(fromname, "-") != 0))
        main_filechanged = FALSE;

cmd_stackptr = 0;
last_se = NULL;
last_gse = last_abese = NULL;
last_gnt = last_abent = NULL;
main_proclist = NULL;
cut_buffer = NULL;
cmd_cbufferline = NULL;
main_undelete = main_lastundelete = NULL;
main_undeletecount = 0;
par_begin = par_end = NULL;
files_written = NULL;

/* There may be additional file names, to be loaded into other buffers. */

  {
  int i;
  cmdstr cmd;
  stringstr str;
  bufferstr *firstbuffer = main_bufferchain;
  for (i = 0; i < MAX_FROM; i++)
    {
    if (main_fromlist[i] == NULL) break;
    str.text = main_fromlist[i];
    cmd.arg1.string = &str;
    cmd.flags |= cmdf_arg1;
    if (e_newbuffer(&cmd) != done_continue) break;
    }
  init_selectbuffer(firstbuffer, TRUE);
  }

return TRUE;
}



/*************************************************
*          Given help on command syntax          *
*************************************************/

static void givehelp(void)
{
printf("NE %s %s using PCRE %s\n%s\n\n", version_string, version_date,
  version_pcre, version_copyright);
printf("-from <files>  input files, default null, - means stdin, key can be omitted\n");
printf("-to <file>     output file for 1st input, default = from\n");
printf("-with <file>   command file, default is terminal\n");
printf("-ver <file>    verification file, default is screen\n");
printf("-line          run in line-by-line mode\n");
printf("-opt <string>  initial line of commands\n");
printf("-noinit        don\'t obey .nerc file\n");
printf("-notraps       disable crash traps\n");
printf("-b[inary]      run in binary mode\n");
printf("-r[eadonly]    start in readonly state\n");
printf("-tabs          expand input tabs; retab those lines on output\n");
printf("-tabin         expand input tabs; no tabs on output\n");
printf("-tabout        use tabs in all output lines\n");
printf("-notabs        no special tab treatment\n");
printf("-notraps       don't catch signals (debugging option)\n");
printf("-w[idechars]   recognize UTF-8 characters in files\n");
printf("--version      show current version\n");
printf("-v[ersion]     show current version\n");
printf("-id            show current version\n");
printf("--help         output this help\n");
printf("-h[elp]        output this help\n");

printf("\nThe tabbing default is -tabs unless overridden by the NETABS "
       "environment\nvariable.\n\n");

printf("          EXAMPLES\n");
printf("ne myfile -notabs\n");
printf("ne myfile -with commands -to outfile -line\n");
}



/*************************************************
*             Decode command line                *
*************************************************/

static void decode_command(int argc, char **argv)
{
enum { arg_from,     arg_to=MAX_FROM, arg_id,        arg_help,   arg_line,
       arg_with,     arg_ver,         arg_opt,       arg_noinit, arg_tabs,
       arg_tabin,    arg_tabout,      arg_notabs,    arg_binary,
       arg_notraps,  arg_readonly,    arg_widechars, arg_end };

int i, rc;
uschar argstring[256];
arg_result results[100];

#define STR(s) #s
#define XSTR(s) STR(s)
Ustrcpy(argstring,
  "from/"
  XSTR(MAX_FROM)
  ",to/k,id=-version=version=v/s,help=-help=h/s,line/s,with/k,ver/k,"
  "opt/k,noinit/s,tabs/s,tabin/s,tabout/s,notabs/s,binary=b/s,"
  "notraps/s,readonly=r/s,widechars=w/s");
#undef STR
#undef XSTR

rc = rdargs(argc, argv, argstring, results);

if (rc != 0)
  {
  main_screenmode = main_screenOK = FALSE;
  error_moan(0, results[0].data.text, results[1].data.text);
  }

/* Deal with "id" and "help" */

if (results[arg_id].data.number != 0)
  {
  printf("NE %s %s using PCRE %s\n", version_string, version_date,
    version_pcre);
  exit(EXIT_SUCCESS);
  }

if (results[arg_help].data.number != 0)
  {
  givehelp();
  exit(EXIT_SUCCESS);
  }

/* Flag to skip obeying initialization string if requested */

if (results[arg_noinit].data.number != 0) main_noinit = TRUE;

/* Handle tabbing options; the defaults are set from a system variable, but if
any options keywords are given, they define the entire state relative to
everything being FALSE. */

if (results[arg_tabs].data.number != 0)
  { main_tabin = main_tabflag = TRUE; main_tabout = FALSE; }

if (results[arg_tabin].data.number != 0)
  {
  main_tabin = TRUE;
  main_tabflag = FALSE;
  main_tabout = (results[arg_tabout].data.number != 0);
  }

else if (results[arg_tabout].data.number != 0)
  { main_tabin = FALSE; main_tabout = TRUE; }

if (results[arg_notabs].data.number != 0)
  { main_tabin = main_tabout = FALSE; }

/* Force line-by-line mode if requested */

if (results[arg_line].data.number != 0)
  main_screenmode = main_screenOK = FALSE;

/* Binary options */

if (results[arg_binary].data.number != 0)
  main_binary = main_overstrike = TRUE;

/* Readonly option */

if (results[arg_readonly].data.number != 0) main_readonly = TRUE;

/* Widechars option */

if (results[arg_widechars].data.number != 0) allow_wide = TRUE;

/* Notraps option */

if (results[arg_notraps].data.number != 0) no_signal_traps = TRUE;

/* Deal with an initial opt command line */

main_opt = results[arg_opt].data.text;

/* Set up a command file - this implies line-by-line mode */

if (results[arg_with].data.text != NULL)
  {
  main_screenmode = main_screenOK = main_interactive = FALSE;
  arg_with_name = store_copystring(results[arg_with].data.text);
  }

/* Set up a verification output file - this implies line-by-line mode */

if (results[arg_ver].data.text != NULL)
  {
  arg_ver_name = store_copystring(results[arg_ver].data.text);
  main_screenmode = main_screenOK = main_interactive = FALSE;
  if (Ustrcmp(arg_ver_name, "-") != 0)
    {
    msgs_fid = sys_fopen(arg_ver_name, US"w");
    if (msgs_fid == NULL)
      {
      msgs_fid = stderr;
      error_moan(5, arg_ver_name, "writing", strerror(errno));  /* Hard because not initialized */
      }
    }
  }

/* Deal with "from" & "to" */

if (results[arg_from].data.text != NULL)
  {
  arg_from_name = arg_from_buffer;
  Ustrcpy(arg_from_name, results[arg_from].data.text);
  if (Ustrcmp(arg_from_name, "-") == 0 &&
    (arg_with_name != NULL && Ustrcmp(arg_with_name, "-") == 0))
      {
      main_screenmode = FALSE;
      error_moan(60, "-from or -with", "input");    /* Hard */
      }
  }

/* Now deal with additional arguments to "from". There may be up to
MAX_FROM in total. */

for (i = 1; i < MAX_FROM; i++) main_fromlist[i-1] = NULL;

for (i = 1; i < MAX_FROM; i++)
  {
  if (results[arg_from + i].data.text == NULL) break;
  main_fromlist[i-1] = results[arg_from+i].data.text;
  }

/* Now "to" */

if (results[arg_to].data.text != NULL)
  {
  arg_to_name = arg_to_buffer;
  Ustrcpy(arg_to_name, results[arg_to].data.text);
  if (Ustrcmp(arg_to_name, "-") == 0)
    {
    if (arg_ver_name == NULL) msgs_fid = stderr;
    else if (Ustrcmp(arg_ver_name, "-") == 0)
      {
      main_screenmode = FALSE;
      error_moan(60, "-to or -ver", "output");    /* Hard */
      }
    }
  }
}


/*************************************************
*             Initialize keystrings              *
*************************************************/

static void keystrings_init(void)
{
int i;
for (i = 0; i <= max_keystring; i++) main_keystrings[i] = NULL;
key_setfkey(1, US"buffer");
/* key_setfkey(2, US"window"); */
key_setfkey(3, US"w");
key_setfkey(4, US"undelete");
key_setfkey(6, US"pll");
key_setfkey(7, US"f");
key_setfkey(8, US"m*");
key_setfkey(9, US"show keys");
key_setfkey(10, US"rmargin");
key_setfkey(11, US"pbuffer");
key_setfkey(16, US"plr");
key_setfkey(17, US"bf");
key_setfkey(18, US"m0");
key_setfkey(19, US"show fkeys");
key_setfkey(20, US"format");
key_setfkey(30, US"unformat");
key_setfkey(57, US"front");
key_setfkey(58, US"topline");
key_setfkey(59, US"back");
key_setfkey(60, US"overstrike");
}


/*************************************************
*            Initialize tables                   *
*************************************************/

/* The character table is initialized dynamically, just in case we ever want
to port this to a non-ascii system. It has to be writable, because the user
can change the definition of what constitutes a "word". */

static void tables_init(void)
{
int i;
uschar *ucletters = US"ABCDEFGHIJKLMNOPQRSTUVWXYZ";
uschar *lcletters = US"abcdefghijklmnopqrstuvwxyz";
uschar *digits    = US"0123456789";
uschar *hexdigits = US"0123456789ABCDEFabcdef";
uschar *delims    = US",.:\'\"!+-*/";

for (i = 0; i < 26; i++)
  {
  ch_tab[lcletters[i]] = ch_lcletter + ch_word;
  ch_tab[ucletters[i]] = ch_ucletter + ch_word;
  }
for (i = 0; i < 10; i++) ch_tab[digits[i]] = ch_digit + ch_word;
for (i = 0; i < 22; i++) ch_tab[hexdigits[i]] |= ch_hexch;
for (i = 0; i < (int)Ustrlen((const uschar *)delims); i++)
  ch_tab[delims[i]] |= (ch_delim + ch_filedelim);

for (i = 0; i < (int)Ustrlen(cmd_qualletters); i++)
  ch_tab[cmd_qualletters[i]] =
  ch_tab[cmd_qualletters[i]] | ch_qualletter;

/* Table to translate a single letter key name into a "control code". This
table is used only in implementing the KEY command in a way that is independent
the machine's character set */

for (i = 0; i < 26; i++)
  {
  key_codes[ucletters[i]] = i+1;
  key_codes[lcletters[i]] = i+1;
  }
key_codes['\\']  = 28;
key_codes[']']   = 29;
key_codes['^']   = 30;
key_codes['_']   = 31;
}


/*************************************************
*            Obey initialization file            *
*************************************************/

/* Function to obey an initialization file. The existence of the file has been
tested by system-specific code. Any errors that arise will be treated as
disasters.

Argument:    the file name
Returns:     nothing (errors cause bomb out)
*/

void
obey_init(uschar *filename)
{
FILE *f = sys_fopen(filename, US"r");
if (f == NULL) error_moan(5, filename, "reading", strerror(errno));
while (Ufgets(cmd_buffer, CMD_BUFFER_SIZE, f) != NULL) cmd_obey(cmd_buffer);
fclose(f);
}



/*************************************************
*            Run in line-by-line mode            *
*************************************************/

static void main_runlinebyline(void)
{
uschar *fromname = arg_from_name;
uschar *toname = (arg_to_name == NULL)? arg_from_name : arg_to_name;

if (main_interactive)
  {
  printf("NE %s %s using PCRE %s\n", version_string, version_date,
    version_pcre);
  main_verify = main_shownlogo = TRUE;
  }
else
  {
  main_verify = FALSE;
  if (arg_with_name != NULL && Ustrcmp(arg_with_name, "-") != 0)
    {
    cmdin_fid = sys_fopen(arg_with_name, US"r");
    if (cmdin_fid == NULL)
      error_moan(5, arg_with_name, "reading", strerror(errno));  /* hard because not initialized */
    }
  }

arg_from_name = arg_to_name = NULL;
currentbuffer = main_bufferchain = NULL;

if (init_init(NULL, fromname, toname))
  {
  if (!main_noinit && main_einit != NULL) obey_init(main_einit);

  main_initialized = TRUE;
  if (main_opt != NULL) cmd_obey(main_opt);

  while (!main_done)
    {
    main_cicount = 0;
    (void)main_interrupted(ci_read);
    if (main_verify) line_verify(main_current, TRUE, TRUE);
    signal(SIGINT, fgets_sigint_handler);
    if (main_interactive) main_rc = error_count = 0;
    if (setjmp(rdline_env) == 0)
      {
      int n;
      if (cmdin_fid == NULL ||
          Ufgets(cmd_buffer, CMD_BUFFER_SIZE, cmdin_fid) == NULL)
        {
        Ustrcpy(cmd_buffer, "w\n");
        }
      main_flush_interrupt();           /* resets handler */
      n = Ustrlen(cmd_buffer);
      if (n > 0 && cmd_buffer[n-1] == '\n') cmd_buffer[n-1] = 0;
      (void)cmd_obey(cmd_buffer);
      }
    else
      {
      if (!main_interactive) break;
      clearerr(cmdin_fid);
      }
    }

  if (cmdin_fid != NULL && cmdin_fid != stdin) fclose(cmdin_fid);
  }
}


/*************************************************
*          Set up tab options                    *
*************************************************/

/* All 3 flags are FALSE on entry; main_tabs is set by default, but may be
overridden by the NETABS environment variable.  */

static void tab_init(void)
{
if (main_tabs == NULL || Ustrcmp(main_tabs, "notabs") == 0) return;
if (Ustrcmp(main_tabs, "tabs") == 0) main_tabin = main_tabflag = TRUE;
else if (Ustrcmp(main_tabs, "tabin") == 0) main_tabin = TRUE;
else if (Ustrcmp(main_tabs, "tabout") == 0) main_tabout = TRUE;
else if (Ustrcmp(main_tabs, "tabinout") == 0) main_tabin = main_tabout = TRUE;
}



/*************************************************
*           Exit tidy-up function                *
*************************************************/
                                                  
/* Automatically called for any exit. Close the input file if it is open, then
free the extensible buffers and other memory.                          
                                                                               
Arguments: none                                                              
Returns:   nothing                            
*/
                                
static void tidy_up(void)
{                
if (debug_file != NULL) fclose(debug_file);
if (crash_logfile != NULL) fclose(crash_logfile);

/* The PCRE2 free() functions do nothing if the argument is NULL (not
initialised) so there's no need to check. */

#ifndef USE_PCRE1
pcre2_general_context_free(re_general_context);
pcre2_compile_context_free(re_compile_context);
pcre2_match_data_free(re_match_data);
#endif

sys_tidy_up();
store_free_all();
}


/*************************************************
*                Entry Point                     *
*************************************************/

int main(int argc, char **argv)
{
int sigptr = 0;
uschar cbuffer[CMD_BUFFER_SIZE];
uschar fbuffer1[FNAME_BUFFER_SIZE];
uschar fbuffer2[FNAME_BUFFER_SIZE];
uschar fbuffer3[FNAME_BUFFER_SIZE];

if (atexit(tidy_up) != 0) error_moan(68);  /* Hard */

cmd_buffer = cbuffer;            /* make globally available */
arg_from_buffer = fbuffer1;      /* make available to functions above */
arg_to_buffer = fbuffer2;

kbd_fid = cmdin_fid = stdin;     /* defaults */
msgs_fid = stdout;

setvbuf(msgs_fid, NULL, _IONBF, 0);

store_init();                    /* Initialize store handler */
tables_init();                   /* Initialize tables */
keystrings_init();               /* Set up default variable keystrings */

sys_init1();                     /* Early local initialization */
version_init();                  /* Set up for messages */
tab_init();                      /* Set default tab options */
arg_zero = US argv[0];           /* Some systems want this */
decode_command(argc, argv);      /* Decode command line */
if (main_binary && allow_wide) error_moan(64);  /* Hard */
sys_init2(fbuffer3);             /* Final local initialization */

if ((arg_from_name != NULL && Ustrcmp(arg_from_name, "-") == 0) ||
    (arg_to_name != NULL && Ustrcmp(arg_to_name, "-") == 0))
      main_interactive = main_screenmode = main_screenOK = FALSE;

/* Set handler to trap keyboard interrupts */

signal(SIGINT, sigint_handler);

/* Set handler for all other relevant signals to dump buffers and exit from NE.
The list of signals is system-dependent. Trapping these interrupts can be cut
out when debugging so the original cause of a fault can more easily be found. */

if (!no_signal_traps)
  {
  while (signal_list[sigptr] > 0) signal(signal_list[sigptr++], crash_handler);
  }

/* The variables main_screenmode and main_interactive will have been set to
indicate the style of editing run. */

msgs_tty = isatty(fileno(msgs_fid));

if (main_screenmode) 
  { 
  cmdin_fid = NULL; 
  sys_runscreen(); 
  }
else main_runlinebyline();

if (main_screenOK && main_nlexit && main_pendnl) sys_mprintf(msgs_fid, "\r\n");
return sys_rc(main_rc);
}

/* End of einit.c */
