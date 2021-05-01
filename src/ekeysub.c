/*************************************************
*       The E text editor - 3rd incarnation      *
*************************************************/

/* Copyright (c) University of Cambridge, 1991 - 2011 */

/* Written by Philip Hazel, starting November 1991 */
/* This file last modified: April 2011 */


/* This file contains code for subroutines connected with
keystroke handling */


#include "ehdr.h"
#include "cmdhdr.h"
#include "keyhdr.h"


static uschar *keystring;



/*************************************************
*       Subroutines for key_set                  *
*************************************************/

static void keymoan(uschar *p, uschar *format, ...)
{
uschar buff[256];
va_list ap;
va_start(ap, format);
vsprintf(CS buff, CS format, ap);
error_moan(14, p - keystring, buff);
va_end(ap);
}


static uschar *getkey(uschar *p, int *ak)
{
uschar *e1 = US"   Key name or number expected";
uschar *e2 = US"   \"%s%s\" cannot be independently configured in this "
               "version of NE\n   (%s)";
*ak = -1;    /* default bad */

mac_skipspaces(p);

if (*p == 0)
  {
  keymoan(p, e1);
  return p;
  }

/* Deal with a number, indicating a function key */

if (isdigit(*p))
  {
  int chcode = 0;
  while (*p != 0 && isdigit(*p)) 
    chcode = chcode*10 + *p++ - '0';

  if (1 <= chcode && chcode <= max_fkey)
    {
    if ((key_functionmap & (1L << chcode)) == 0)
      keymoan(p, US"   Function key %d not available in this version of E", chcode);
    else *ak = chcode + s_f_umax;
    }
  else keymoan(p, US"   Incorrect function key number (not in range 1-%d)", max_fkey);
  }

/* Deal with a single letter key name, indicating ctrl/<something> */

else if (!isalpha(p[1]) && p[1] != '/')
  {
  int chcode = key_codes[*p++];
  if (chcode == 0) keymoan(p, e1);
  else if ((key_controlmap & (1L << chcode)) == 0)
    {
    uschar name[8];
    Ustrcpy(name, "ctrl/");
    name[5] = p[-1];
    name[6] = 0;
    keymoan(p, e2, US"", name, sys_keyreason(chcode));
    }
  else *ak = chcode;
  }

/* Deal with multi-letter key name, possibly preceded by "s/" or "c/" */

else
  {
  uschar name[20];
  int n = 0;
  int chcode = -1;
  int shiftbits = 0;

  while(p[1] == '/')
    {
    if (*p == 's') shiftbits += s_f_shiftbit; else
      {
      if (*p == 'c') shiftbits += s_f_ctrlbit;
        else keymoan(p, US"   s/ or c/ expected");
      }
    p += 2;
    if (*p == 0) break;
    }

  while (isalpha(*p)) name[n++] = *p++;
  name[n] = 0;

  for (n = 0; key_names[n].name[0] != 0; n++)
    if (Ustrcmp(name, key_names[n].name) == 0)
      { chcode = key_names[n].code; break; }

  if (chcode > 0)
    {
    int mask = (int)(1L << ((chcode-s_f_ubase)/4));

    if ((key_specialmap[shiftbits] & mask) == 0)
      {
      uschar *sname = US"";
      switch (shiftbits)
        {
        case s_f_shiftbit: sname = US"s/"; break;
        case s_f_ctrlbit:  sname = US"c/"; break;
        case s_f_shiftbit+s_f_ctrlbit: sname = US"s/c/"; break;
        }
      keymoan(p, e2, sname, name, sys_keyreason(chcode+shiftbits));
      }
    else *ak = chcode + shiftbits;
    }
  else keymoan(p, US"   %s is not a valid key name", name);
  }

return p;
}



static uschar *getaction(uschar *p, int *aa)
{
*aa = -1;    /* default bad */

mac_skipspaces(p);

if (*p == 0 || *p == ',') 
  {
  *aa = 0;    /* The unset action */
  }  
   
else if (isalpha(*p))
  {
  int n = 0;
  char name[8];
  while (isalpha(*p)) name[n++] = *p++;
  name[n] = 0;

  for (n = 0; n < key_actnamecount; n++)
    if (Ustrcmp(key_actnames[n].name, name) == 0)
      { *aa = key_actnames[n].code; break; }

  if (*aa < 0) keymoan(p, US"   Unknown key action");
  }

else if (isdigit(*p))
  {
  int code = 0;
  while (isdigit(*p)) code = code*10 + *p++ - '0';
  if (1 <= code && code <= max_keystring) *aa = code; else
    keymoan(p, US"   Incorrect function keystring number (not in range 1-%d)", max_keystring);
  }

else keymoan(p, US"   Key action (letters or a number) expected");

return p;
}


/*************************************************
*    Process setting string for keystrokes       *
*************************************************/

/* This procedure is called twice for every KEY command -
during decoding to check the syntax, and during execution
to do the job. This is a bit wasteful, but it is a rare
enough operation, and it saves having variable-length
argument lists to commands. */

int key_set(uschar *string, BOOL goflag)
{
uschar *p = string;

if (!main_screenmode) return TRUE;

keystring = string;

while (*p)
  {
  int a, k;
  p = getkey(p, &k);
  if (k < 0) return FALSE;
  mac_skipspaces(p);
  if (*p != '=' && *p != ':')
    {
    keymoan(p, US"   Equals sign or colon expected");
    return FALSE;
    }
  p = getaction(++p, &a);
  if (a < 0) return FALSE;
  mac_skipspaces(p);
  if (*p != 0 && *p != ',')
    {
    keymoan(p, US"   Comma expected");
    return FALSE;
    }
  if (*p) p++;

  if (goflag)
    {
    key_table[k] = a;
    }
  }

return TRUE;
}


/*************************************************
*           Generate name of special key         *
*************************************************/

void key_makespecialname(int key, uschar *buff)
{
uschar *s1 = US"  ";
uschar *s2 = s1;

buff[0] = 0;
if ((key & s_f_shiftbit) != 0) { Ustrcat(buff, US"s/"); s1 = US""; }
if ((key & s_f_ctrlbit)  != 0) { Ustrcat(buff, US"c/"); s2 = US""; }

Ustrcat(buff, key_specialnames[(key - s_f_ubase) >> 2]);
Ustrcat(buff, s1);
Ustrcat(buff, s2);
}


/* End of ekeysub.c */
