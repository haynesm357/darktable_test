/*
    This file is part of darktable,
    copyright (c) 2009--2010 johannes hanika.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "common/darktable.h"
#include "common/history.h"
#include "common/debug.h"
#include "control/control.h"
#include "control/conf.h"
#include "control/jobs.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/hist_dialog.h"
#include "libs/lib.h"
#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "dtgtk/button.h"

DT_MODULE(1)

typedef struct dt_lib_copy_history_t
{
  int32_t imageid;
  GtkComboBoxText *pastemode;
  GtkButton *paste, *paste_parts;
  GtkWidget *copy_button, *delete_button, *load_button, *write_button;
  GtkWidget *copy_parts_button;

  dt_gui_hist_dialog_t dg;
} dt_lib_copy_history_t;

const char *name()
{
  return _("history stack");
}

uint32_t views()
{
  return DT_VIEW_LIGHTTABLE;
}

uint32_t container()
{
  return DT_UI_CONTAINER_PANEL_RIGHT_CENTER;
}

static void write_button_clicked(GtkWidget *widget, dt_lib_module_t *self)
{
  dt_control_write_sidecar_files();
}

static void load_button_clicked(GtkWidget *widget, dt_lib_module_t *self)
{
  GtkWidget *win = dt_ui_main_window(darktable.gui->ui);
  GtkWidget *filechooser = gtk_file_chooser_dialog_new(
      _("open sidecar file"), GTK_WINDOW(win), GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, (char *)NULL);

  GtkFileFilter *filter;
  filter = GTK_FILE_FILTER(gtk_file_filter_new());
  gtk_file_filter_add_pattern(filter, "*.xmp");
  gtk_file_filter_set_name(filter, _("XMP sidecar files"));
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filechooser), filter);

  filter = GTK_FILE_FILTER(gtk_file_filter_new());
  gtk_file_filter_add_pattern(filter, "*");
  gtk_file_filter_set_name(filter, _("all files"));
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(filechooser), filter);

  if(gtk_dialog_run(GTK_DIALOG(filechooser)) == GTK_RESPONSE_ACCEPT)
  {
    char *dtfilename;
    dtfilename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(filechooser));

    if(dt_history_load_and_apply_on_selection(dtfilename) != 0)
    {
      GtkWidget *dialog
          = gtk_message_dialog_new(GTK_WINDOW(win), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
                                   GTK_BUTTONS_CLOSE, _("error loading file '%s'"), dtfilename);
      gtk_dialog_run(GTK_DIALOG(dialog));
      gtk_widget_destroy(dialog);
    }

    g_free(dtfilename);
  }
  gtk_widget_destroy(filechooser);
  gtk_widget_queue_draw(dt_ui_center(darktable.gui->ui));
}

static int get_selected_image(void)
{
  int imgid;

  /* get imageid for source if history past */
  sqlite3_stmt *stmt;
  DT_DEBUG_SQLITE3_PREPARE_V2(dt_database_get(darktable.db), "select * from selected_images", -1, &stmt, NULL);
  if(sqlite3_step(stmt) == SQLITE_ROW)
  {
    /* copy history of first image in selection */
    imgid = sqlite3_column_int(stmt, 0);
    // dt_control_log(_("history of first image in selection copied"));
  }
  else
  {
    /* no selection is used, use mouse over id */
    imgid = dt_control_get_mouse_over_id();
  }
  sqlite3_finalize(stmt);

  return imgid;
}

static void copy_button_clicked(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;

  d->imageid = get_selected_image();

  if(d->imageid > 0)
  {
    d->dg.selops = NULL;
    d->dg.copied_imageid = d->imageid;

    gtk_widget_set_sensitive(GTK_WIDGET(d->paste), TRUE);
    gtk_widget_set_sensitive(GTK_WIDGET(d->paste_parts), TRUE);
  }
}

static void copy_parts_button_clicked(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;

  d->imageid = get_selected_image();

  if(d->imageid > 0)
  {
    d->dg.copied_imageid = d->imageid;

    // launch dialog to select the ops to copy
    int res = dt_gui_hist_dialog_new(&(d->dg), d->imageid, TRUE);

    if(res != GTK_RESPONSE_CANCEL && d->dg.selops)
    {
      gtk_widget_set_sensitive(GTK_WIDGET(d->paste), TRUE);
      gtk_widget_set_sensitive(GTK_WIDGET(d->paste_parts), TRUE);
    }
  }
}

static void delete_button_clicked(GtkWidget *widget, gpointer user_data)
{
  dt_history_delete_on_selection();
  dt_control_queue_redraw_center();
}

static void paste_button_clicked(GtkWidget *widget, gpointer user_data)
{

  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;

  /* get past mode and store, overwrite / merge */
  int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(d->pastemode));
  dt_conf_set_int("plugins/lighttable/copy_history/pastemode", mode);

  /* copy history from d->imageid and past onto selection */
  if(dt_history_copy_and_paste_on_selection(d->imageid, (mode == 0) ? TRUE : FALSE, d->dg.selops) != 0)
  {
    /* no selection is used, use mouse over id */
    int32_t mouse_over_id = dt_control_get_mouse_over_id();
    if(mouse_over_id <= 0) return;

    dt_history_copy_and_paste_on_image(d->imageid, mouse_over_id, (mode == 0) ? TRUE : FALSE, d->dg.selops);
  }

  /* redraw */
  dt_control_queue_redraw_center();
}

static void paste_parts_button_clicked(GtkWidget *widget, gpointer user_data)
{
  dt_lib_module_t *self = (dt_lib_module_t *)user_data;
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;

  // launch dialog to select the ops to paste
  if(dt_gui_hist_dialog_new(&(d->dg), d->dg.copied_imageid, FALSE) == GTK_RESPONSE_OK)
    paste_button_clicked(widget, user_data);
}

static void pastemode_combobox_changed(GtkWidget *widget, gpointer user_data)
{
  int mode = gtk_combo_box_get_active(GTK_COMBO_BOX(widget));
  dt_conf_set_int("plugins/lighttable/copy_history/pastemode", mode);
}

void gui_reset(dt_lib_module_t *self)
{
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;
  d->imageid = -1;
  gtk_widget_set_sensitive(GTK_WIDGET(d->paste), FALSE);
}

int position()
{
  return 600;
}

void gui_init(dt_lib_module_t *self)
{
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)malloc(sizeof(dt_lib_copy_history_t));
  self->data = (void *)d;
  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  dt_gui_hist_dialog_init(&d->dg);

  GtkBox *hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));

  GtkWidget *copy_parts = gtk_button_new_with_label(_("copy"));
  d->copy_parts_button = copy_parts;
  g_object_set(G_OBJECT(copy_parts), "tooltip-text",
               _("copy part history stack of\nfirst selected image (ctrl-shift-c)"), (char *)NULL);
  gtk_box_pack_start(hbox, copy_parts, TRUE, TRUE, 0);

  GtkWidget *copy = gtk_button_new_with_label(_("copy all"));
  d->copy_button = copy;
  g_object_set(G_OBJECT(copy), "tooltip-text", _("copy history stack of\nfirst selected image (ctrl-c)"),
               (char *)NULL);
  gtk_box_pack_start(hbox, copy, TRUE, TRUE, 0);

  GtkWidget *delete = gtk_button_new_with_label(_("discard"));
  d->delete_button = delete;
  g_object_set(G_OBJECT(delete), "tooltip-text", _("discard history stack of\nall selected images"),
               (char *)NULL);
  gtk_box_pack_start(hbox, delete, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), TRUE, TRUE, 0);
  hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));

  d->paste_parts = GTK_BUTTON(gtk_button_new_with_label(_("paste")));
  g_object_set(G_OBJECT(d->paste_parts), "tooltip-text",
               _("paste part history stack to\nall selected images (ctrl-shift-v)"), (char *)NULL);
  d->imageid = -1;
  gtk_widget_set_sensitive(GTK_WIDGET(d->paste_parts), FALSE);
  gtk_box_pack_start(hbox, GTK_WIDGET(d->paste_parts), TRUE, TRUE, 0);

  d->paste = GTK_BUTTON(gtk_button_new_with_label(_("paste all")));
  g_object_set(G_OBJECT(d->paste), "tooltip-text", _("paste history stack to\nall selected images (ctrl-v)"),
               (char *)NULL);
  gtk_widget_set_sensitive(GTK_WIDGET(d->paste), FALSE);
  gtk_box_pack_start(hbox, GTK_WIDGET(d->paste), TRUE, TRUE, 0);

  d->pastemode = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
  gtk_combo_box_text_append_text(d->pastemode, _("append"));
  gtk_combo_box_text_append_text(d->pastemode, _("overwrite"));
  g_object_set(G_OBJECT(d->pastemode), "tooltip-text", _("how to handle existing history"), (char *)NULL);
  gtk_box_pack_start(hbox, GTK_WIDGET(d->pastemode), TRUE, TRUE, 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(d->pastemode),
                           dt_conf_get_int("plugins/lighttable/copy_history/pastemode"));

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), TRUE, TRUE, 0);

  hbox = GTK_BOX(gtk_hbox_new(TRUE, 5));
  GtkWidget *loadbutton = gtk_button_new_with_label(_("load sidecar file"));
  d->load_button = loadbutton;
  g_object_set(G_OBJECT(loadbutton), "tooltip-text",
               _("open an XMP sidecar file\nand apply it to selected images"), (char *)NULL);
  gtk_box_pack_start(hbox, loadbutton, TRUE, TRUE, 0);

  GtkWidget *button = gtk_button_new_with_label(_("write sidecar files"));
  d->write_button = button;
  g_object_set(G_OBJECT(button), "tooltip-text", _("write history stack and tags to XMP sidecar files"),
               (char *)NULL);
  gtk_box_pack_start(hbox, button, TRUE, TRUE, 0);
  g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(write_button_clicked), (gpointer)self);

  gtk_box_pack_start(GTK_BOX(self->widget), GTK_WIDGET(hbox), TRUE, TRUE, 0);

  g_signal_connect(G_OBJECT(copy), "clicked", G_CALLBACK(copy_button_clicked), (gpointer)self);
  g_signal_connect(G_OBJECT(copy_parts), "clicked", G_CALLBACK(copy_parts_button_clicked), (gpointer)self);
  g_signal_connect(G_OBJECT(delete), "clicked", G_CALLBACK(delete_button_clicked), (gpointer)self);
  g_signal_connect(G_OBJECT(d->paste_parts), "clicked", G_CALLBACK(paste_parts_button_clicked),
                   (gpointer)self);
  g_signal_connect(G_OBJECT(d->paste), "clicked", G_CALLBACK(paste_button_clicked), (gpointer)self);
  g_signal_connect(G_OBJECT(loadbutton), "clicked", G_CALLBACK(load_button_clicked), (gpointer)self);
  g_signal_connect(G_OBJECT(d->pastemode), "changed", G_CALLBACK(pastemode_combobox_changed), (gpointer)self);
}

void gui_cleanup(dt_lib_module_t *self)
{
  free(self->data);
  self->data = NULL;
}

void init_key_accels(dt_lib_module_t *self)
{
  dt_accel_register_lib(self, NC_("accel", "copy all"), GDK_KEY_c, GDK_CONTROL_MASK);
  dt_accel_register_lib(self, NC_("accel", "copy"), GDK_KEY_c, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
  dt_accel_register_lib(self, NC_("accel", "discard"), 0, 0);
  dt_accel_register_lib(self, NC_("accel", "paste all"), GDK_KEY_v, GDK_CONTROL_MASK);
  dt_accel_register_lib(self, NC_("accel", "paste"), GDK_KEY_v, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
  dt_accel_register_lib(self, NC_("accel", "load sidecar files"), 0, 0);
  dt_accel_register_lib(self, NC_("accel", "write sidecar files"), 0, 0);
}

void connect_key_accels(dt_lib_module_t *self)
{
  dt_lib_copy_history_t *d = (dt_lib_copy_history_t *)self->data;

  dt_accel_connect_button_lib(self, "copy all", GTK_WIDGET(d->copy_button));
  dt_accel_connect_button_lib(self, "copy", GTK_WIDGET(d->copy_parts_button));
  dt_accel_connect_button_lib(self, "discard", GTK_WIDGET(d->delete_button));
  dt_accel_connect_button_lib(self, "paste all", GTK_WIDGET(d->paste));
  dt_accel_connect_button_lib(self, "paste", GTK_WIDGET(d->paste_parts));
  dt_accel_connect_button_lib(self, "load sidecar files", GTK_WIDGET(d->load_button));
  dt_accel_connect_button_lib(self, "write sidecar files", GTK_WIDGET(d->write_button));
}
// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
