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

static pinentry_t pinentry;

static int pinentry_window_done;

enum action {
  PINENTRY_ACTION_CANCEL,
  PINENTRY_ACTION_OK,
  PINENTRY_ACTION_NOTOK
};


static gboolean
close_request (GtkWindow *w, gpointer data)
{
  (void)w;
  (void)data;

  pinentry->close_button = 1;
  pinentry_window_done = 1;
  return TRUE;
}

static void
enter_callback (GtkPasswordEntry *e, gpointer data)
{
  const char *s;
  int len;

  (void)e;
  (void)data;
  s = gtk_editable_get_text (GTK_EDITABLE (e));
  len = strlen (s);
  pinentry_setbufferlen (pinentry, len + 1);
  if (pinentry->pin)
    strcpy (pinentry->pin, s);
  pinentry->result = len;

  pinentry->canceled = 0;
  pinentry_window_done = 1;
}

static void
clicked (GtkButton *button, gpointer data)
{
  int action = (int)(intptr_t)data;

  (void)button;
  if (action == PINENTRY_ACTION_CANCEL)
    {
      pinentry->canceled = 1;
      pinentry->result = -1;
    }
  else if (action == PINENTRY_ACTION_OK)
    pinentry->result = 1;
  else
    pinentry->result = 0;

  pinentry_window_done = 1;
}

#define VSPACE 25
#define HSPACE 20

/* FIXME: title support */
/* FIXME: Add LIBSECRET support (HAVE_LIBSECRET) to cache password.  */
/* FIXME: Timeout support */
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

  pinentry = pe;
  pinentry_window_done = 0;

  w = gtk_window_new ();
  g_signal_connect (G_OBJECT (w), "close-request",
		    G_CALLBACK (close_request), NULL);


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
                        G_CALLBACK (enter_callback), NULL);

      if (pe->quality_bar)
        {
          q = gtk_level_bar_new ();
          gtk_grid_attach (GTK_GRID (g), q, 0, 1, 2, 1);
          if (pe->repeat_passphrase)
            {
              p2 = gtk_label_new (pe->repeat_passphrase);
              gtk_grid_attach (GTK_GRID (g), p2, 0, 2, 1, 1);
              e2 = gtk_password_entry_new ();
              gtk_grid_attach (GTK_GRID (g), e2, 1, 2, 1, 1);
              puts ("Q!");
            }
        }

      gtk_box_append (GTK_BOX (v), g);
    }

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, HSPACE);
  if (!pe->one_button)
    {
      b_cancel = gtk_button_new_with_label (pe->cancel);
      gtk_box_append (GTK_BOX (h), b_cancel);
      g_signal_connect (G_OBJECT (b_cancel), "clicked", G_CALLBACK (clicked),
                        (gpointer)PINENTRY_ACTION_CANCEL);
    }
  if (pe->notok)
    {
      b_notok = gtk_button_new_with_label (pe->notok);
      gtk_box_append (GTK_BOX (h), b_notok);
      g_signal_connect (G_OBJECT (b_notok), "clicked", G_CALLBACK (clicked),
                        (gpointer)PINENTRY_ACTION_NOTOK);
    }
  b_ok = gtk_button_new_with_label (pe->ok);
  gtk_box_append (GTK_BOX (h), b_ok);
  g_signal_connect (G_OBJECT (b_ok), "clicked", G_CALLBACK (clicked),
                    (gpointer)PINENTRY_ACTION_OK);

  gtk_box_append (GTK_BOX (v), h);

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
    ->ok
    ->error
    ->prompt

    ->cancel
    ->timeout
    ->one_button
    ->confirm
   */
  /*
    ->canceled
   */
  /*
    genpin_label
   */
  /*
    ->repeat_passphrase
    ->repeat_error_string
    ->repeat_ok_string
   */
  /* quality bar
     b = gtk_level_bar_new ();
     gtk_levelbar_add_set_value (GTK_LEVEL_BAR (b), 0.8);
   */
  gtk_window_present (GTK_WINDOW (w));

  if (pe->confirm)
    {
      gtk_widget_grab_focus (b_ok);
    }

  while (!pinentry_window_done)
    g_main_context_iteration (NULL, TRUE);

  gtk_window_destroy (GTK_WINDOW (w));

  pinentry = NULL;
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
