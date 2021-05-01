/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2018 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: February 2018 */


/* This file contains code for handling input and output */

#include "ehdr.h"

#define buffgetsize 1024


/*************************************************
*          Support routines for backups          *
*************************************************/

BOOL file_written(uschar *name)
{
filewritstr *p = files_written;
while (p != NULL)
  {
  #ifdef FILE_CASELESS
  uschar *s = name;
  uschar *t = p->name;  
  while ((*t || *s) && tolower(*t) == tolower(*s)) { t++; s++; }
  if (*t == 0 && *s == 0) return TRUE; 
  #else  
  if (Ustrcmp(name, p->name) == 0) return TRUE;
  #endif 
  p = p->next; 
  }
return FALSE;
}     

void file_setwritten(uschar *name)
{
filewritstr *p;
if (file_written(name)) return;
p = store_Xget(sizeof(filewritstr));
p->name = store_copystring(name);
p->next = files_written;
files_written = p;
}



/*************************************************
*      Get next input line and binarize it       *
*************************************************/

static linestr *file_nextbinline(FILE **f, int *binoffset)
{
FILE *ff = *f;
linestr *line = store_getlbuff(80);
int c = EOF;

if (ff != NULL)
  {
  int i;
  uschar *s = line->text;
  uschar *p = s + 8;
  uschar cc[17];
  cc[16] = 0;

  sprintf(CS s, "%06x  ", *binoffset);
  *binoffset += 16;

  for (i = 0; i < 16; i++)
    {
    c = fgetc(ff);
    if (c == EOF)
      {
      if (i == 0) { line->len = 0; goto READ; }
      sprintf(CS p, "   ");
      }
    else sprintf(CS p, "%02x ", c);
    p += 3;
    if (i == 7) *p++ = ' ';
    cc[i] = isprint(c)? c : '.';
    }
  sprintf(CS p, " * %s *", cc);
  line->len = (p - s + 21);
  }
else line->len = 0;

READ:
if (c == EOF)
  {
  if (line->len == 0) line->flags |= lf_eof;
  if (ff != NULL)
    {
    fclose(ff);
    *f = NULL;
    }
  }

return line;
}



/*************************************************
*            Get next input line                 *
*************************************************/

/* Returns an eof buffer at end of file. We get a buffer of large(ish) size,
and free off the end of it if possible. When a line is very long, we have
to copy it into a longer buffer. The binoffset variable is non-null when we
want to read a "binary line". */

linestr *file_nextline(FILE **f, int *binoffset)
{
BOOL eof = FALSE;
BOOL tabbed = FALSE;
FILE *ff;
int length, maxlength;
linestr *line;
uschar *s;

if (main_binary && binoffset != NULL)
  line = file_nextbinline(f, binoffset);
else
  {
  ff = *f;
  length = 0;
  maxlength = buffgetsize;
  line = store_getlbuff(buffgetsize);
  s = line->text;

  if (ff == NULL) eof = TRUE; else while (length < buffgetsize)
    {
    int c = fgetc(ff);
    if (c == '\n') break;
    if (c == EOF)
      {
      if (length == 0 || ferror(ff)) eof = TRUE;
      break;
      }
    if (c == '\t' && main_tabin)
      {
      tabbed = main_tabflag;
      do
        {
        *s++ = ' ';
        length++;
        if (length >= buffgetsize) break;
        }
      while ((length % 8) != 0);
      }
    else { *s++ = c; length++; }
    }

  line->len = length;

  /* Deal with lines longer than the buffer size. If we run out of store,
  the line gets chopped. */

  while (length >= maxlength)
    {
    uschar *newtext;
    
    if (length > MAX_LINELENGTH)
      {
      if (main_initialized) 
        error_moan(66, MAX_LINELENGTH);
      else
        error_moan(67, MAX_LINELENGTH);    
      break;
      }     

    newtext = store_get(length+buffgetsize);
    if (newtext == NULL) 
      {
      error_moan(1, length+buffgetsize); 
      break;
      } 
 
    memcpy(newtext, line->text, length);
    store_free(line->text);
    line->text = newtext;
    s = line->text + length;
    maxlength += buffgetsize;

    while (length < maxlength)
      {
      int c = fgetc(ff);
      if (c == '\n' || c == EOF) break;
      if (c == '\t' && main_tabin)
        {
        tabbed = main_tabflag;
        do
          {
          *s++ = ' ';
          length++;
          if (length >= maxlength) break;
          }
        while ((length % 8) != 0);
        }
      else { *s++ = c; length++; }
      }

    line->len = length;
    }

  /* Free up unwanted store at end of buffer, or free the whole buffer if
  this is an empty line. */

  if (length > 0) store_chop(line->text, length); else
    {
    store_free(line->text);
    line->text = NULL;
    }

  /* At end of file, close input */

  if (eof)
    {
    line->flags |= lf_eof;
    if (ff != NULL)
      {
      fclose(ff);
      *f = NULL;
      }
    }

  if (tabbed) line->flags |= lf_tabs;
  }

return line;
}


/*************************************************
*           Write a line's characters            *
*************************************************/

int file_writeline(linestr *line, FILE *f)
{
int i;
int len = line->len;
uschar *p = line->text;

/* Handle binary output */

if (main_binary)
  {
  BOOL ok = TRUE; 
  while (len > 0 && isxdigit((usint)(*p))) { len--; p++; }

  while (len-- > 0)
    {
    int cc;
    int c = *p++;
    if (c == ' ') continue;
    if (c == '*') break;

    if ((ch_tab[c] & ch_hexch) != 0)
      {
      int uc = toupper(c);
      cc = (isalpha(uc)?  uc - 'A' + 10 : uc - '0') << 4;
      }
    else 
      {
      error_moan(58, c);
      ok = FALSE; 
      continue;  
      }

    c = *p++;
    len--;
    if ((ch_tab[c] & ch_hexch) != 0)
      {
      int uc = toupper(c);
      cc += isalpha(uc)?  uc - 'A' + 10 : uc - '0';
      }
    else { error_moan(58, c); ok = FALSE; }

    fputc(cc, f);
    }
    
  return ok? 1 : 0;
  }

/* Handle normal output; we need to scan the line only if it is
to have tabs inserted into it. First check for detrailing. */

if (main_detrail_output)
  {
  uschar *pp = p + len - 1;
  while (len > 0 && *pp-- == ' ') len--; 
  } 

if (main_tabout || (line->flags & lf_tabs) != 0)
  {
  for (i = 0; i < len; i++)
    {
    int c = p[i];

    /* Search for string of 2 or more spaces, ending at tabstop */

    if (c == ' ')
      {
      int n; 
      int k = i + 1;
      while (k < len) { if (p[k] != ' ') break; k++; }
      while (k > i + 1 && (k & 7) != 0) k--;

      if ((n = (k - i)) > 1 && (k & 7) == 0)
        {
        /* Found suitable string - use tab(s) and skip spaces */
        c = '\t';
        i = k - 1;
        for (; n > 8; n -= 8) fputc('\t', f);
        }
      }
    fputc(c, f);
    }
  }

/* Untabbed line -- optimize */

else if(fwrite(p, 1, len, f)){};  /* Avoid compiler warning, but should check? */

/* Add final LF */

fputc('\n', f);

/* Check that the line was successfully written, and yield result */

return ferror(f)? (-1) : (+1);
}



/*************************************************
*           Write current buffer to file         *
*************************************************/

BOOL file_save(uschar *name)
{
FILE *f;
linestr *line = main_top;
int yield = TRUE;

if (name == NULL || name[0] == 0)
  { error_moan(59, currentbuffer->bufferno); return FALSE; }
else if (Ustrcmp(name, "-") == 0) f = stdout;
else if ((f = sys_fopen(name, US"w")) == NULL) 
  { error_moan(5, name, "writing", strerror(errno)); return FALSE; }

while ((line->flags & lf_eof) == 0)
  {
  int rc = file_writeline(line, f);
  if (rc < 0)
    {
    error_moan(37, name, strerror(errno));
    return FALSE;
    }
  else if (rc == 0) yield = FALSE;   /* Binary failure */
  line = line->next;
  }

if (f != stdout) 
  {
  if (fclose(f) != 0)
    {
    error_moan(37, name, strerror(errno));
    return FALSE;
    }
  } 
   
return yield;
}

/* End of efile.c */
