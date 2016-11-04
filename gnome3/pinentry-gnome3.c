/* pinentry-gnome3.c
   Copyright (C) 2015 g10 Code GmbH

   pinentry-gnome-3 is a pinentry application for GNOME 3.  It tries
   to follow the Gnome Human Interface Guide as close as possible.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <gcr/gcr-base.h>

#include <string.h>
#include <stdlib.h>

#include <assuan.h>

#include "memory.h"

#include "pinentry.h"

#ifdef FALLBACK_CURSES
#include "pinentry-curses.h"
#endif


#define PGMNAME "pinentry-gnome3"

#ifndef VERSION
#  define VERSION
#endif

static gchar *
pinentry_utf8_validate (gchar *text)
{
  gchar *result;

  if (!text)
    return NULL;

  if (g_utf8_validate (text, -1, NULL))
    return g_strdup (text);

  /* Failure: Assume that it was encoded in the current locale and
     convert it to utf-8.  */
  result = g_locale_to_utf8 (text, -1, NULL, NULL, NULL);
  if (!result)
    {
      gchar *p;

      result = p = g_strdup (text);
      while (!g_utf8_validate (p, -1, (const gchar **) &p))
	*p = '?';
    }
  return result;
}

struct _gnome3_run {
  pinentry_t pinentry;
  GcrPrompt *prompt;
  GMainLoop *main_loop;
  int ret;
  guint timeout_id;
  int timed_out;
};

static void
_gcr_prompt_password_done (GObject *source_object, GAsyncResult *res, gpointer user_data);

static void
_gcr_prompt_confirm_done (GObject *source_object, GAsyncResult *res, gpointer user_data);

static gboolean
_gcr_timeout_done (gpointer user_data);

static void
_propagate_g_error_to_pinentry (pinentry_t pe, GError *error,
                                gpg_err_code_t code, const char *loc)
{
  size_t infolen = strlen(error->message) + 20;

  pe->specific_err = gpg_error (code);
  pe->specific_err_info = malloc (infolen);
  if (pe->specific_err_info)
    snprintf (pe->specific_err_info, infolen,
              "%d: %s", error->code, error->message);
  pe->specific_err_loc = loc;
}

static GcrPrompt *
create_prompt (pinentry_t pe, int confirm)
{
  GcrPrompt *prompt;
  GError *error = NULL;
  char *msg;
  char window_id[32];

  /* Create the prompt.  */
  prompt = GCR_PROMPT (gcr_system_prompt_open (pe->timeout ? pe->timeout : -1, NULL, &error));
  if (! prompt)
    {
      /* this means the timeout elapsed, but no prompt was ever shown. */
      if (error->code == GCR_SYSTEM_PROMPT_IN_PROGRESS)
        {
          fprintf (stderr, "Timeout: the Gcr system prompter was already in use.\n");
          pe->specific_err_info = strdup ("Timeout: the Gcr system prompter was already in use.");
          /* not using GPG_ERR_TIMEOUT here because the user never saw
             a prompt: */
          pe->specific_err = gpg_error (GPG_ERR_PIN_ENTRY);
        }
      else
        {
          fprintf (stderr, "couldn't create prompt for gnupg passphrase: %s\n",
                   error->message);
          _propagate_g_error_to_pinentry (pe, error, GPG_ERR_CONFIGURATION,
                                          "gcr_system_prompt_open");
        }
      g_error_free (error);
      return NULL;
    }

  /* Set the messages for the various buttons, etc.  */
  if (pe->title)
    {
      msg = pinentry_utf8_validate (pe->title);
      gcr_prompt_set_title (prompt, msg);
      g_free (msg);
    }

  if (pe->description)
    {
      msg = pinentry_utf8_validate (pe->description);
      gcr_prompt_set_description (prompt, msg);
      g_free (msg);
    }

  /* An error occurred during the last prompt.  */
  if (pe->error)
    {
      msg = pinentry_utf8_validate (pe->error);
      gcr_prompt_set_warning (prompt, msg);
      g_free (msg);
    }

  if (! pe->prompt && confirm)
    gcr_prompt_set_message (prompt, "Message");
  else if (! pe->prompt && ! confirm)
    gcr_prompt_set_message (prompt, "Enter Passphrase");
  else
    {
      msg = pinentry_utf8_validate (pe->prompt);
      gcr_prompt_set_message (prompt, msg);
      g_free (msg);
    }

  if (! confirm)
    gcr_prompt_set_password_new (prompt, !!pe->repeat_passphrase);

  if (pe->ok || pe->default_ok)
    {
      msg = pinentry_utf8_validate (pe->ok ?: pe->default_ok);
      gcr_prompt_set_continue_label (prompt, msg);
      g_free (msg);
    }
  /* XXX: Disable this button if pe->one_button is set.  */
  if (pe->cancel || pe->default_cancel)
    {
      msg = pinentry_utf8_validate (pe->cancel ?: pe->default_cancel);
      gcr_prompt_set_cancel_label (prompt, msg);
      g_free (msg);
    }

  if (confirm && pe->notok)
    {
      /* XXX: Add support for the third option.  */
    }

  /* gcr expects a string; we have a int.  see gcr's
     ui/frob-system-prompt.c for example conversion using %lu */
  snprintf (window_id, sizeof (window_id), "%lu",
            (long unsigned int)pe->parent_wid);
  window_id[sizeof (window_id) - 1] = '\0';
  gcr_prompt_set_caller_window (prompt, window_id);

#ifdef HAVE_LIBSECRET
  if (! confirm && pe->allow_external_password_cache && pe->keyinfo)
    {
      if (pe->default_pwmngr)
	{
	  msg = pinentry_utf8_validate (pe->default_pwmngr);
	  gcr_prompt_set_choice_label (prompt, msg);
	  g_free (msg);
	}
      else
	gcr_prompt_set_choice_label
	  (prompt, "Automatically unlock this key, whenever I'm logged in");
    }
#endif

  return prompt;
}

static int
gnome3_cmd_handler (pinentry_t pe)
{
  struct _gnome3_run state;

  state.main_loop = g_main_loop_new (NULL, FALSE);
  if (!state.main_loop)
    {
      pe->specific_err_info = strdup ("Failed to create GMainLoop");
      pe->specific_err = gpg_error (GPG_ERR_PIN_ENTRY);
      pe->specific_err_loc = "g_main_loop_new";
      pe->canceled = 1;
      return -1;
    }
  state.pinentry = pe;
  state.ret = 0;
  state.timeout_id = 0;
  state.timed_out = 0;
  state.prompt = create_prompt (pe, !!(pe->pin));
  if (!state.prompt)
    {
      pe->canceled = 1;
      return -1;
    }
  if (pe->pin)
    gcr_prompt_password_async (state.prompt, NULL, _gcr_prompt_password_done,
                               &state);
  else
    gcr_prompt_confirm_async (state.prompt, NULL, _gcr_prompt_confirm_done,
                              &state);

  if (pe->timeout)
    state.timeout_id = g_timeout_add_seconds (pe->timeout, _gcr_timeout_done, &state);
  g_main_loop_run (state.main_loop);

  /* clean up state: */
  if (state.timeout_id && !state.timed_out)
    g_source_destroy (g_main_context_find_source_by_id (NULL, state.timeout_id));
  g_clear_object (&state.prompt);
  g_main_loop_unref (state.main_loop);
  return state.ret;
};


static void
_gcr_prompt_password_done (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  struct _gnome3_run *state = (struct _gnome3_run *) user_data;
  GcrPrompt *prompt = GCR_PROMPT (source_object);

  if (state && prompt && state->prompt == prompt)
    {
      const char *password;
      GError *error = NULL;
      pinentry_t pe = state->pinentry;
      int ret = -1;

      /* "The returned password is valid until the next time a method
	 is called to display another prompt."  */
      password = gcr_prompt_password_finish (prompt, res, &error);
      if ((! password && ! error)
          || (error && error->code == G_IO_ERROR_CANCELLED))
        {
          /* operation was cancelled or timed out.  */
          ret = -1;
          if (state->timed_out)
            state->pinentry->specific_err = gpg_error (GPG_ERR_TIMEOUT);
          if (error)
            g_error_free (error);
        }
      else if (error)
	{
          _propagate_g_error_to_pinentry (pe, error,
                                          GPG_ERR_PIN_ENTRY,
                                          "gcr_system_password_finish");
	  g_error_free (error);
	  ret = -1;
	}
      else
	{
	  pinentry_setbufferlen (pe, strlen (password) + 1);
	  if (pe->pin)
	    strcpy (pe->pin, password);

	  if (pe->repeat_passphrase)
	    pe->repeat_okay = 1;

#ifdef HAVE_LIBSECRET
	  if (pe->allow_external_password_cache && pe->keyinfo)
	    pe->may_cache_password = gcr_prompt_get_choice_chosen (prompt);
#endif

	  ret = 1;
	}
      state->ret = ret;
    }

  if (state)
    g_main_loop_quit (state->main_loop);
}

static void
_gcr_prompt_confirm_done (GObject *source_object, GAsyncResult *res,
                          gpointer user_data)
{
  struct _gnome3_run *state = (struct _gnome3_run *) user_data;
  GcrPrompt *prompt = GCR_PROMPT (source_object);

  if (state && prompt && state->prompt == prompt)
    {
      GcrPromptReply reply;
      GError *error = NULL;
      pinentry_t pe = state->pinentry;
      int ret = -1;

      /* XXX: We don't support a third button!  */

      reply = gcr_prompt_confirm_finish (prompt, res, &error);
      if (error)
	{
          if (error->code == G_IO_ERROR_CANCELLED)
            {
              pe->canceled = 1;
              if (state->timed_out)
                state->pinentry->specific_err = gpg_error (GPG_ERR_TIMEOUT);
            }
          else
            _propagate_g_error_to_pinentry (state->pinentry, error, GPG_ERR_PIN_ENTRY,
                                            "gcr_system_confirm_finish");
	  g_error_free (error);
	  ret = 0;
	}
      else if (reply == GCR_PROMPT_REPLY_CONTINUE
	       /* XXX: Hack since gcr doesn't yet support one button
		  message boxes treat cancel the same as okay.  */
	       || pe->one_button)
        {
          /* Confirmation.  */
          ret = 1;
        }
      else /* GCR_PROMPT_REPLY_CANCEL */
	{
	  pe->canceled = 1;
          if (state->timed_out)
            state->pinentry->specific_err = gpg_error (GPG_ERR_TIMEOUT);
	  ret = 0;
	}
      state->ret = ret;
    }

  if (state)
    g_main_loop_quit (state->main_loop);
}

static gboolean
_gcr_timeout_done (gpointer user_data)
{
  struct _gnome3_run *state = (struct _gnome3_run *) user_data;

  if (!state)
    return FALSE;

  state->timed_out = 1;
  gcr_prompt_close (state->prompt);

  return FALSE;
}

pinentry_cmd_handler_t pinentry_cmd_handler = gnome3_cmd_handler;

int
main (int argc, char *argv[])
{
  pinentry_init (PGMNAME);

#ifdef FALLBACK_CURSES
  if (!getenv ("DBUS_SESSION_BUS_ADDRESS"))
    {
      fprintf (stderr, "No $DBUS_SESSION_BUS_ADDRESS found,"
               " falling back to curses\n");
      pinentry_cmd_handler = curses_cmd_handler;
    }
#endif

  pinentry_parse_opts (argc, argv);

  if (pinentry_loop ())
    return 1;

  return 0;
}
