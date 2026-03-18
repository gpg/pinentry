/* pinentry-gtk.c
 * Copyright (C) 2023 g10 Code GmbH
 *
 * pinentry-gtk is a pinentry application for the GTK toolkit.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 * SPDX-License-Identifier: GPL-2.0+
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <gtk/gtk.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gpg-error.h>

#include "pinentry.h"

#ifdef FALLBACK_CURSES
#include "pinentry-curses.h"
#endif


#define PGMNAME "pinentry-gtk"

#ifndef VERSION
#  define VERSION
#endif

enum action {
  PINENTRY_ACTION_CANCEL,
  PINENTRY_ACTION_OK,
  PINENTRY_ACTION_NOTOK
};


static gboolean
close_request (GtkWindow *w, gpointer data)
{
  pinentry_t pe = data;

  (void)w;

  pe->result = -1;
  pe->close_button = 1;
  return TRUE;
}

static void
enter_callback (GtkPasswordEntry *e, gpointer data)
{
  pinentry_t pe = data;

  (void)e;

  pe->result = 1;
}

static void
clicked_cancel (GtkButton *button, gpointer data)
{
  pinentry_t pe = data;

  pe->result = -1;
  pe->canceled = 1;
}

static void
clicked_notok (GtkButton *button, gpointer data)
{
  pinentry_t pe = data;

  (void)button;
  pe->result = -1;
  pe->specific_err = 0;
}

static void
clicked_ok (GtkButton *button, gpointer data)
{
  pinentry_t pe = data;

  (void)button;
  pe->result = 1;
}

static gboolean
timeout_cb (gpointer data)
{
  pinentry_t pe = data;

  pe->result = -1;
  pe->specific_err = gpg_error (GPG_ERR_TIMEOUT);
  return FALSE;
}

#define VSPACE 25
#define HSPACE 20

/* FIXME: title support */
/* FIXME: Add LIBSECRET support (HAVE_LIBSECRET) to cache password.  */
/* FIXME: GENPIN support */
/*
 [ description ]
 [ error notification ]
 grid:
 ---------------------------------------------------
 [ prompt ]           [ entry 1 ]
 [ quality bar ]
 [ repeat_passphrase] [ entry 2 (repeat) ]
 ---------------------------------------------------
 [ cancel ] [ not-ok ] [ ok ]
 */
static int
gtk_cmd_handler (pinentry_t pe)
{
  GtkWidget *w, *v, *d, *n, *g, *h;
  GtkWidget *p1, *e1, *q, *p2, *e2;
  GtkWidget *b_cancel, *b_notok, *b_ok;
  char *title;
  const char *ok_str = pe->ok ? pe->ok: pe->default_ok;
  guint timeout_id = 0;

  pe->result = 0;
  pe->canceled = 0;
  pe->close_button = 0;

  e1 = NULL;

  w = gtk_window_new ();
  g_signal_connect (G_OBJECT (w), "close-request",
                    G_CALLBACK (close_request), pe);

  v = gtk_box_new (GTK_ORIENTATION_VERTICAL, VSPACE);
  gtk_widget_set_margin_top (v, VSPACE);
  gtk_widget_set_margin_start (v, HSPACE);
  gtk_widget_set_margin_end (v, HSPACE);
  gtk_widget_set_margin_bottom (v, VSPACE);

  d = gtk_label_new (pe->description);
  gtk_box_append (GTK_BOX (v), d);

  if (pe->error)
    n = gtk_label_new (pe->error);
  else if (pe->repeat_passphrase)
    n = gtk_label_new ("");
  else
    n = NULL;

  if (n)
    gtk_box_append (GTK_BOX (v), n);

  if (!pe->confirm)
    {
      g = gtk_grid_new ();

      gtk_grid_set_column_spacing (GTK_GRID (g), HSPACE);
      gtk_grid_set_row_spacing (GTK_GRID (g), VSPACE);

      if (pe->prompt)
        {
          p1 = gtk_label_new (pe->prompt);
          gtk_grid_attach (GTK_GRID (g), p1, 0, 0, 1, 1);
        }

      e1 = gtk_password_entry_new ();
      gtk_grid_attach (GTK_GRID (g), e1, 1, 0, 1, 1);

      g_signal_connect (G_OBJECT (e1), "activate",
                        G_CALLBACK (enter_callback), pe);

      if (pe->quality_bar)
        {
          /*
            gtk_levelbar_add_set_value (GTK_LEVEL_BAR (b), 0.8);
          */
          q = gtk_level_bar_new ();
          gtk_grid_attach (GTK_GRID (g), q, 0, 1, 2, 1);
        }

      if (pe->repeat_passphrase)
        {
          p2 = gtk_label_new (pe->repeat_passphrase);
          gtk_grid_attach (GTK_GRID (g), p2, 0, 2, 1, 1);
          e2 = gtk_password_entry_new ();
          gtk_grid_attach (GTK_GRID (g), e2, 1, 2, 1, 1);
        }

      gtk_box_append (GTK_BOX (v), g);
    }

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, HSPACE);
  if (!pe->one_button)
    {
      const char *cancel_str = pe->cancel ? pe->cancel: pe->default_cancel;

      cancel_str = cancel_str ? cancel_str : "Cancel";
      b_cancel = gtk_button_new_with_mnemonic (cancel_str);
      gtk_box_append (GTK_BOX (h), b_cancel);
      g_signal_connect (G_OBJECT (b_cancel), "clicked",
                        G_CALLBACK (clicked_cancel),
                        pe);
    }
  if (pe->notok)
    {
      b_notok = gtk_button_new_with_mnemonic (pe->notok);
      gtk_box_append (GTK_BOX (h), b_notok);
      g_signal_connect (G_OBJECT (b_notok), "clicked",
                        G_CALLBACK (clicked_notok),
                        pe);
    }
  b_ok = gtk_button_new_with_mnemonic (ok_str? ok_str : "OK");
  gtk_box_append (GTK_BOX (h), b_ok);
  g_signal_connect (G_OBJECT (b_ok), "clicked", G_CALLBACK (clicked_ok), pe);

  gtk_box_append (GTK_BOX (v), h);

  title = pinentry_get_title (pe);
  if (title)
    {
      gtk_window_set_title (GTK_WINDOW (w), title);
      free (title);
    }
  gtk_window_set_modal (GTK_WINDOW (w), TRUE);
  gtk_window_set_child (GTK_WINDOW (w), v);
  /*
    dialog
    transient
    fullscreen
   */
  /*
    key accel (Escape to cancel, etc.???)
   */
  /*
    ->error
    ->confirm
   */
  /*
    ->canceled
   */
  /*
    ->genpin_label
   */
  /*
    ->repeat_error_string
    ->repeat_ok_string
   */
  /*
    ->*_tt
   */
  gtk_window_present (GTK_WINDOW (w));

  if (pe->confirm)
    {
      gtk_widget_grab_focus (b_ok);
    }

  if (pe->timeout > 0)
    timeout_id = g_timeout_add (pe->timeout * 1000, timeout_cb, pe);

  while (!pe->result)
    g_main_context_iteration (NULL, TRUE);

  if (pe->result == -1 && pe->specific_err == 0
      && pe->canceled == 0 && pe->close_button == 0)
    /* The action was "Not OK" */
    pe->result = 0;
  else if (e1 && pe->result == 1)
    {
      const char *s;
      int len;

      s = gtk_editable_get_text (GTK_EDITABLE (e1));
      len = strlen (s);
      pinentry_setbufferlen (pe, len + 1);
      if (pe->pin)
        strcpy (pe->pin, s);
      pe->result = len;
    }

  gtk_window_destroy (GTK_WINDOW (w));

  if (pe->timeout > 0 && gpg_err_code (pe->specific_err) != GPG_ERR_TIMEOUT)
    g_source_remove (timeout_id);

  return pe->result;
}


pinentry_cmd_handler_t pinentry_cmd_handler = gtk_cmd_handler;


int
main (int argc, char *argv[])
{
  pinentry_init (PGMNAME);

#ifdef FALLBACK_CURSES
  if (!gtk_init_check ())
    {
      pinentry_cmd_handler = curses_cmd_handler;
      pinentry_set_flavor_flag ("curses");
    }
#else
  gtk_init ();
#endif

  pinentry_parse_opts (argc, argv);

  if (pinentry_loop ())
    return 1;

  return 0;
}
