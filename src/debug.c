/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2016 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: January 2016 */


/* This file contains debugging code. */

#include "ehdr.h"


/*************************************************
*           Display debugging output             *
*************************************************/

/* Made global so it can be called from debugging code elsewhere */

void debug_printf(const char *format, ...)
{
uschar buff[256];
va_list ap;
va_start(ap, format);
vsprintf(CS buff, format, ap);
if (main_screenmode)
  {
  if (debug_file == NULL)
    {
    debug_file = fopen("NEdebug", "w");
    if (debug_file == NULL)
      {
      printf("\n\n***** Can't open debug file - aborting *****\n\n");
      exit(99);
      }
    }
  fprintf(debug_file, "%s", buff);
  fflush(debug_file);
  }
else
  {
  printf("%s", buff);
  fflush(stdout);
  }
va_end(ap);
}


/*************************************************
*        Give information about the screen       *
*************************************************/

void debug_screen(void)
{
debug_printf("main_linecount         = %d\n", main_linecount);
debug_printf("cursor_offset      = %2d\n", cursor_offset);
debug_printf("cursor_row         = %2d cursor_col         = %2d\n",
  cursor_row, cursor_col);
debug_printf("window_width       = %2d window_depth       = %2d\n",
  window_width, window_depth);
debug_printf("-------------------------------------------------\n");
}


/*************************************************
*            Write to a crash log file           *
*************************************************/

void debug_writelog(const char *format, ...)
{
va_list ap;
va_start(ap, format);

if (crash_logfile == NULL)
  {
  uschar *name = sys_crashfilename(FALSE);
  if (name == NULL) return; 
  crash_logfile = Ufopen(name, "w");
  if (crash_logfile == NULL)
    {
    main_logging = FALSE;    /* Prevent recursion */ 
    error_printf("Failed to open crash log file %s\n", name);
    return;
    }
  }

vfprintf(crash_logfile, format, ap);
fflush(crash_logfile);
va_end(ap);
}

/* End of debug.c */
