/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2016 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: December 2016 */


/* This file contains code for matching a search expression. The global
variables match_leftpos and match_rightpos must be set to the byte offsets
within which to search. */


#include "ehdr.h"



/*************************************************
*            Check 'word'                        *
*************************************************/

static BOOL chkword(int p, int len, uschar *t, int wleft, int wright)
{
int n = p + len;
if (p > wleft && (ch_tab[t[p-1]] & ch_word) != 0) return FALSE;
if (n < wright && (ch_tab[t[n]] & ch_word) != 0) return FALSE;
return TRUE;
}



/*************************************************
*         Match char string to line string       *
*************************************************/

static BOOL matchchars(uschar *s, int len, uschar *t, BOOL U)
{
int i;
if (U)
  {
  if (allow_wide)
    {
    for (i = 0; i < len; i++)
      {
      if (s[i] > 127 || t[i] > 127)
        {
        if (s[i] != t[i]) return FALSE;
        }
      else
        {
        if (toupper(s[i]) != toupper(t[i])) return FALSE;
        }
      }
    }
  else
    for (i = 0; i < len; i++) if (toupper(s[i]) != toupper(t[i])) return FALSE;
  }
else
  {
  for (i = 0; i < len; i++) if (s[i] != t[i]) return FALSE;
  }

return TRUE;
}



/*************************************************
*        Match qualified string to line          *
*************************************************/

static int matchqs(qsstr *qs, linestr *line, int USW)
{
uschar *t = line->text;
uschar *s = qs->text + 1;       /* first (byte) char is delimiter */

usint p;                                             /* match point */
usint count = qs->count;
usint flags = qs->flags;
usint len = qs->length;
usint leftpos = match_leftpos;                     /* lhs byte in line */
usint rightpos = match_rightpos;                   /* rhs byte in line */
usint wleft = line_offset(line, qs->windowleft);   /* lhs window byte offset */
usint wright = line_offset(line, qs->windowright); /* rhs window byte offset */
usint *map = qs->map;

BOOL U = (qs->flags & qsef_U) != 0 ||
  ((USW & qsef_U) != 0 && (qs->flags & qsef_V) == 0); /* upper-case state */
BOOL W = ((qs->flags | USW) & qsef_W) != 0;           /* wordsearch state */
BOOL yield = MATCH_FAILED;                            /* default reply */

/* If hex string, adjust pointers */

if ((flags & qsef_X) != 0)
  {
  s = qs->hexed;
  len /= 2;
  }

/* Take note of line length & significant space qualifier */

if (wright > line->len) wright = line->len;
if (((flags | USW) & qsef_S) != 0)
  {
  while (wleft < wright && t[wleft] == ' ') wleft++;
  while (wleft < wright && t[wright-1] == ' ') wright--;
  }

/* Take note of window qualifier */

if (leftpos < wleft) leftpos = wleft;
if (rightpos > wright) rightpos = wright;
if (rightpos < leftpos) rightpos = leftpos;

/* In most cases, search starts at left */

p = leftpos;

/* First check line long enough, then match according to the flags */

if ((leftpos + len <= rightpos))
  {

  /* Deal with B & P (P = B + E); if H is on it must be PH as
  H is not allowed with either B or E on its own. */

  if ((flags & qsef_B) != 0)
    {
    if (p == wleft || (flags & qsef_H) != 0)    /* must be at line start or H */
      {
      /* Deal with P */

      if ((flags & qsef_E) != 0)
        {
        if (len == rightpos - p && matchchars(s, len, t+p, U)) 
          yield = MATCH_OK;
        }

      /* Deal with B on its own */

      else if (matchchars(s, len, t+p, U) &&
        (!W || chkword(p, len, t, p, wright))) yield = MATCH_OK;
      }
    }

  /* Deal with E on its own */

  else if ((qs ->flags & qsef_E) != 0)
    {
    if (rightpos == wright)   /* must be at line end */
      {
      p = rightpos - len;
      if (matchchars(s, len, t+p, U) &&
        (!W || chkword(p, len, t, wleft, wright))) yield = MATCH_OK;
      }
    }

  /* Deal with H */

  else if ((qs-> flags & qsef_H) != 0)
    {
    if (matchchars(s, len, t+p, U) &&
      (!W || chkword(p, len, t, wleft, wright))) yield = MATCH_OK;
    }

  /* Deal with L; if the line character at the first position is not
  in the string, skip backwards the whole length. */

  else if (match_L || (flags & qsef_L) != 0)
    {
    p = rightpos - len;
    if (len == 0) yield = MATCH_OK; else for(;;)
      {
      int c = t[p];
      if ((map[c/intbits] & (1 << (c%intbits))) == 0)
        {
        if (p >= len) p -= len; else break;
        }
      else
        {
        if (matchchars(s, len, t+p, U) &&
            (!W || chkword(p, len, t, wleft, wright)))
          {
          if (--count == 0)  { yield = MATCH_OK; break; }
          if (p >= len) p -= len; else break;
          }
        else
          {
          if (p > leftpos) p--; else break;
          }
        }
      }
    }

  /* Else it's a straight forward search; if the line byte at the
  last position is not in the string, skip forward by the whole length. */

  else
    {
    if (len == 0) yield = MATCH_OK; else while (p + len <= rightpos)
      {
      int c = t[p+len-1];
      if ((map[c/intbits] & (1 << (c%intbits))) == 0) p += len; else
        {
        if (matchchars(s, len, t+p, U) &&
          (!W || chkword(p, len, t, wleft, wright)))
            {
            if (--count <= 0) { yield = MATCH_OK; break; }
            p += len;
            }
        else p++;
        }
      }
    }

  /* If successful non-C match, set start & end */

  if (yield == MATCH_OK)
    {
    match_start = p;
    match_end = p + len;
    }
  }

/* If N is present, reverse result */

if ((flags & qsef_N) != 0)
  {
  yield = (yield == MATCH_OK)? MATCH_FAILED : MATCH_OK;
  if (yield == MATCH_OK)
    {
    match_start = 0;    /* give whole line */
    match_end = line->len;
    }
  }

return yield;
}



/*************************************************
*        Match a search expression to a line     *
*************************************************/

/* The yield is MATCH_OK, MATCH_FAILED or MATCH_ERROR; if OK, the global
variables match_start and match_end contain the start and end of the matched
string (or whole line). The left and right margins for the search are set in
globals to avoid too many arguments. There is also the flag match_L, set during
backwards find operations. */

int cmd_matchse(sestr *se, linestr *line, int USW)
{
int yield;

/* Pass down U, S and W flags -- V turns U off */

if ((se->flags & qsef_U) != 0) USW |= qsef_U;
if ((se->flags & qsef_V) != 0) USW &= ~qsef_U;
if ((se->flags & qsef_S) != 0) USW |= qsef_S;
if ((se->flags & qsef_W) != 0) USW |= qsef_W;

/* Test for qualified string */

if (se->type == cb_qstype)
  return ((se->flags & qsef_R) != 0)?
    cmd_matchqsR((qsstr *)se, line, USW) : matchqs((qsstr *)se, line, USW);

/* Got a search expression node - yield is a line */

yield = cmd_matchse(se->left.se, line, USW);
if (yield == MATCH_ERROR) return yield;

/* Deal with OR */

if ((se->flags & qsef_AND) == 0)
  {
  if (yield == MATCH_FAILED && se->right.se != NULL)
    yield = cmd_matchse(se->right.se, line, USW);
  }

/* Deal with AND */

else if (yield == MATCH_OK) yield = cmd_matchse(se->right.se, line, USW);

if (yield == MATCH_ERROR) return yield;

/* Invert yield if N set */

if ((se->flags & qsef_N) != 0) 
  yield = (yield == MATCH_OK)? MATCH_FAILED : MATCH_OK;

/* If matched, return whole line */

if (yield == MATCH_OK)
  {
  match_start = 0;
  match_end = line->len;
  }

return yield;
}

/* End of ematch.c */
