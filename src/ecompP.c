/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2018 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: May 2018 */


/* This file contains code for interfacing to the PCRE library for handling
regular expressions. A macro determines whether PCRE1 (legacy) or PCRE2 (now
the default) is used. */

#include "ehdr.h"

/* Set the maximum number of extracted strings, and a couple of local variables
that save data about them after a match, for use when doing replacements. It is
assumed that a call to cmd_ReChange happens after a match and before any other
match. */

#define ExtractSize 20
static int ExtractNumber;
static int ExtractStartAt;

/* In PCRE1 we have to supply the ovector (in PCRE2 it is part of the
match_data block). Adjust PCRE flag names. */

#ifdef USE_PCRE1
static int Extracted[ExtractSize*3];
#define RE_ANCHORED PCRE_ANCHORED
#define RE_CASELESS PCRE_CASELESS
#define RE_UTF      PCRE_UTF8
#else
PCRE2_SIZE *Extracted = NULL;
#define RE_ANCHORED PCRE2_ANCHORED
#define RE_CASELESS PCRE2_CASELESS
#define RE_UTF      PCRE2_UTF
#endif


/*************************************************
*  Custom memory management interface for PCRE2  *
*************************************************/

#ifndef USE_PCRE1
static void *re_store_get(PCRE2_SIZE size, void *userdata)
{
(void)userdata;
return store_Xget((int)size);
}

static void re_store_free(void *pointer, void *userdata)
{
(void)userdata;
store_free(pointer);
}
#endif



/*************************************************
*           Compile a Regular Expression         *
*************************************************/

/* NE was originally written with its own regex code; we put in the calls to
PCRE with the minimal of disturbance so that two different versions of NE were
easy to maintain. This makes this a bit awkward in places. Now that only PCRE
support exists, this could be tidied up one day. */

BOOL cmd_makeCRE(qsstr *qs)
{
#ifdef USE_PCRE1
int offset;
#else
int errorcode;
PCRE2_SIZE offset;
#endif

int flags = qs->flags;
int options = ((flags & (qsef_V | qsef_FV)) == 0)? RE_CASELESS : 0;
usint offset_adjust = 0;
const uschar *error;
uschar *temp = NULL;
uschar *temp2 = NULL;
uschar *pattern = qs->text + 1;

/* If the X flag is on, scan the pattern and encode any hexadecimal pairs.
This is 99% OK for realistic cases... */

if ((flags & qsef_X) != 0)
  {
  uschar *p;
  p = temp2 = store_Xget(Ustrlen(pattern)*2 + 1);

  while (*pattern != 0)
    {
    if (isxdigit(*pattern))
      {
      *p++ = '\\';
      *p++ = 'x';
      *p++ = *pattern++;
      if (isxdigit(*pattern)) *p++ = *pattern++;
      }
    else if (*pattern == '\\')
      {
      if (pattern[1] != 'x')
        {
        *p++ = *pattern++;
        *p++ = *pattern++;
        }
      else pattern += 2;
      }
    else *p++ = *pattern++;
    }
  *p = 0;
  pattern = temp2;
  }

/* PCRE1 and PCRE2 handle custom memory managers differently. */

#ifdef USE_PCRE1
/* These really only ever need to be done once, but setting that up
probably costs more than just doing it every time! */

pcre_malloc = (void *(*)(size_t))store_Xget;
pcre_free = store_free;
#else

/* Set up PCRE2 contexts, which are needed for custom memory management, and a
match data block, if they do not already exist. */

if (re_general_context == NULL)
  {
  re_general_context = pcre2_general_context_create(re_store_get,
    re_store_free, 0);
  re_compile_context = pcre2_compile_context_create(re_general_context);
  re_match_data = pcre2_match_data_create(ExtractSize,
    re_general_context);
  Extracted = pcre2_get_ovector_pointer(re_match_data);
  }
#endif

/* NE flags implying "from end" matching can be handled by adding to the
beginning or end of the incoming regex. Remember that this was done. Adjust the
offset printed for errors. */

if ((flags & qsef_L) != 0 ||
    ((match_L || (flags & qsef_E) != 0) && (flags & qsef_B) == 0))
  {
  temp = store_Xget(Ustrlen(pattern) + 6);
  offset_adjust = 3;
  sprintf(CS temp, ".*(%s)%s", pattern, ((flags & qsef_E) != 0)? "$" : "");
  qs->flags |= qsef_REV;
  pattern = temp;
  }
else qs->flags &= ~qsef_REV;

/* The B & H flags can be done by a PCRE flag. */

if ((flags & (qsef_B|qsef_H)) != 0) options |= RE_ANCHORED;

/* When wide characters are in use, we must tell PCRE to use UTF-8 */

if (allow_wide) options |= RE_UTF;

/* Do the compilation */

#ifdef USE_PCRE1
qs->cre = pcre_compile(CS pattern, options, (const char **)(&error), &offset, NULL);
#else
qs->cre = pcre2_compile(pattern, PCRE2_ZERO_TERMINATED, options, &errorcode,
  &offset, re_compile_context);
#endif

if (temp != NULL) store_free(temp);
if (temp2 != NULL) store_free(temp2);

if (qs->cre == NULL)
  {
  #ifndef USE_PCRE1
  uschar error_buffer[256];
  pcre2_get_error_message(errorcode, error_buffer, sizeof(error_buffer));
  error = error_buffer;
  #endif
  if (offset_adjust > (usint)offset) offset = 0; else offset -= offset_adjust;
  error_moan(63, offset, error);
  return FALSE;
  }

return TRUE;
}



/*************************************************
*            Match Regular Expression            *
*************************************************/

/* The result of the match ends up in the static variable, in case needed for
replacement, but put the start and end into the standard globals. */

int cmd_matchqsR(qsstr *qs, linestr *line, int USW)
{
uschar *chars = line->text;
int count = qs->count;
int flags = qs->flags;
int yield = MATCH_FAILED;
usint leftpos = match_leftpos;
usint  rightpos = match_rightpos;
usint wleft = qs->windowleft;
usint wright = qs->windowright;
BOOL backwards = (flags & qsef_L) != 0 || ((match_L || (flags & qsef_E) != 0) &&
  (flags & qsef_B) == 0);

/* If there's no compiled expression, or if it is compiled in the wrong
direction (which can happen if the argument of an F command is reused
with a BF command or vice versa), or if the casing requirements are wrong,
then (re)compile it. */

if (qs->cre == NULL ||
    (backwards && (flags & qsef_REV) == 0) ||
    (!backwards && (flags & qsef_REV) != 0) ||
    ((USW & qsef_U) == 0 && (flags & (qsef_U | qsef_V | qsef_FV)) == 0) ||
    ((USW & qsef_U) != 0 && (flags & qsef_FV) != 0)
   )
  {
  if ((USW & qsef_U) == 0) qs->flags |= qsef_FV; else qs->flags &= ~qsef_FV;
  if (qs->cre != NULL) { store_free(qs->cre); qs->cre = NULL; }
  cmd_makeCRE(qs);
  flags = qs->flags;
  }

/* Take note of line length && sig space qualifier */

if (wright > line->len) wright = line->len;
if ((flags & qsef_S) != 0 || (USW & qsef_S) != 0)
  {
  while (wleft < wright && chars[wleft] == ' ') wleft++;
  while (wleft < wright && chars[wright-1] == ' ') wright--;
  }

/* Take note of window qualifier */

if (leftpos < wleft) leftpos = wleft;
if (rightpos > wright) rightpos = wright;
if (rightpos < leftpos) rightpos = leftpos;

/* The B and E flags can now be tested */

if (((flags & qsef_B) != 0 && leftpos != wleft) ||
  ((flags & qsef_B) == 0 && (flags & qsef_E) != 0 && rightpos != wright))
    yield = MATCH_FAILED;

/* Do the match; we may have to repeat for the count qualifier. */

else if (chars != NULL) for (;;)
  {
  int i;

  #ifdef USE_PCRE1
  ExtractNumber = pcre_exec((const pcre *)qs->cre, NULL,
    CS chars + leftpos, rightpos - leftpos, 0, 0, Extracted,
      sizeof(Extracted)/sizeof(int));
  if (ExtractNumber == PCRE_ERROR_NOMATCH) break;
  if (ExtractNumber < 0)
    {
    const char *detail = (ExtractNumber == PCRE_ERROR_BADUTF8)?
      ": bad UTF-8 string" : ""; 
    error_moan(65, ExtractNumber, detail);
    return MATCH_ERROR;
    }
  #else  /* Using PCRE2 */
  ExtractNumber = pcre2_match(qs->cre, chars + leftpos, rightpos - leftpos,
    0, 0, re_match_data, NULL);
  if (ExtractNumber == PCRE2_ERROR_NOMATCH) break;
  if (ExtractNumber < 0)
    {
    uschar error_buffer[256];
    pcre2_get_error_message(ExtractNumber, error_buffer, sizeof(error_buffer));
    error_moan(65, error_buffer);
    return MATCH_ERROR;
    }
  #endif

  if (ExtractNumber == 0) ExtractNumber = ExtractSize;

  ExtractStartAt = ((flags & qsef_REV) != 0)? 2 : 0;
  for (i = 0; i < ExtractNumber * 2; i++) Extracted[i] += leftpos;

  match_start = Extracted[ExtractStartAt+0];
  match_end = Extracted[ExtractStartAt+1];

  /* Additional check the P qualifier */

  if ((flags & qsef_EB) == qsef_EB && match_end != rightpos) break;

  /* Additional check for W qualifier */

  yield = MATCH_OK;
  if ((flags & qsef_W) != 0 || (USW & qsef_W) != 0)
    {
    if ((match_start != wleft &&
      (ch_tab[chars[match_start-1]] & ch_word) != 0) ||
      (match_end != wright &&
        (ch_tab[chars[match_end]] & ch_word) != 0))
    yield = MATCH_FAILED;
    }

  /* Additional check for the count qualifier */

  if (yield == MATCH_OK && --count <= 0) break;

  /* Skip over this string */

  yield = MATCH_FAILED;
  if ((flags & qsef_REV) != 0) rightpos = match_start;
    else leftpos = match_end;
  if (leftpos >= rightpos) break;
  }

/* The match has either succeeded or failed without error. If the N qualifier
is present, reverse the yield and set the pointers to include the whole line
when it didn't match (successful yield). */

if ((flags & qsef_N) != 0)
  {
  yield = (yield == MATCH_FAILED)? MATCH_OK : MATCH_FAILED;
  if (yield == MATCH_OK)
    {
    match_start = 0;
    match_end = line->len;
    }
  }

return yield;
}


/*************************************************
*         Increase buffer size                   *
*************************************************/

/* This is an auxiliary function for cmd_ReChange(). */

static uschar *incbuffer(uschar *v, usint *sizep)
{
uschar *newv = store_Xget(*sizep + 1024);
memcpy(newv, v, *sizep);
*sizep += 1024;
return newv;
}


/*************************************************
*    Change Line after Regular Expression match  *
*************************************************/

/* This function is called to make changes to a line after it has been matched
with a regular expression. The replacement string is searched for occurences of
the '$' character, which are interpreted as follows:

$0           insert the entire matched string from the original line
$<digit>     insert the <digit>th "captured substring" from the match
$<non-digit> insert <non-digit>

It is also permitted to use this function after a non-regex match, in which
case only $0 causes any insertion. Any other $<digit>s are ignored, and this
also happens in the regex case if there aren't enough captured strings. If (for
example) $6 is encountered and there were fewer than 6 captured strings,
nothing is inserted.

NB: After the change, cursor_col is set to a *byte* offset.

Arguments:
  line        line to change
  p           replacement string
  len         length of replacement string
  hexflag     TRUE if the replacement is a hex string
  eflag       TRUE for "exchange", i.e. first remove the matched string
  aflag       TRUE for "insert after"

Returns:      the changed line
*/

linestr *cmd_ReChange(linestr *line, uschar *p, usint len, BOOL hexflag,
  BOOL eflag, BOOL aflag)
{
usint i, pp;
usint n = 0;
usint size = 1024;
uschar *v = store_Xget(size);

/* Loop to scan replacement string */

for (pp = 0; pp < len; pp++)
  {
  int cc = p[pp];

  /* Ensure at least one byte left in v */

  if (n >= size) v = incbuffer(v, &size);

  /* Deal with non-meta character (not '$') */

  if (cc != '$')
    {
    /* Deal with hexadecimal data */
    if (hexflag)
      {
      int x = 0;
      for (i = 1; i <= 2; i++)
        {
        x = x << 4;
        cc = tolower(cc);
        x += ('a' <= cc && cc <= 'f')? cc - 'a' + 10 : cc - '0';
        cc = p[pp+1];
        }
      pp++;
      v[n++] = x;
      }

    /* Deal with normal data */

    else v[n++] = cc;
    }

  /* Deal with the meta-character ('$') */

  else
    {
    if (++pp < len)
      {
      cc = p[pp];
      if (!isdigit(cc)) v[n++] = cc;
      else
        {
        uschar *ppp = line->text;
        cc -= '0';

        /* Have to deal with 0 specially, since it is allowed even
        if the previous match wasn't a regex. */

        if (cc == 0)
          {
          while (n + match_end - match_start >= size) v = incbuffer(v, &size);
          for (i = match_start; i < match_end; i++) v[n++] = ppp[i];
          }
        else if (cc < ExtractNumber)
          {
          usint x = (usint)Extracted[ExtractStartAt + 2*cc];
          usint y = (usint)Extracted[ExtractStartAt + 2*cc+1];
          while (n + y - x >= size) v = incbuffer(v, &size);
          for (i = x; i < y; i++) v[n++] = ppp[i];
          }
        }
      }
    }
  }

/* We now have built the replacement string in v */

if (eflag)
  {
  line_deletech(line, match_start, match_end - match_start, TRUE);
  line_insertbytes(line, -1, match_start, v, n, 0);
  cursor_col = match_start + n;    /* Byte offset */
  }
else
  {
  line_insertbytes(line, -1, (aflag? match_end : match_start), v, n, 0);
  cursor_col = match_end + n;      /* Byte offset */
  }

store_free(v);
return line;
}

/* End of ecompP.c */
