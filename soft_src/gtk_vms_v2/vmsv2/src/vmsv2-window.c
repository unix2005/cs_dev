/* vmsv2-window.c
 *
 * Copyright 2026 
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "vmsv2-window.h"

struct _Vmsv2Window
{
	GtkApplicationWindow  parent_instance;

	/* Template widgets */
	GtkHeaderBar        *header_bar;
	GtkLabel            *label;
};

G_DEFINE_FINAL_TYPE (Vmsv2Window, vmsv2_window, GTK_TYPE_APPLICATION_WINDOW)

static void
vmsv2_window_class_init (Vmsv2WindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/io/github/unix2005266/gtkvmsv2/vmsv2-window.ui");
	gtk_widget_class_bind_template_child (widget_class, Vmsv2Window, header_bar);
	gtk_widget_class_bind_template_child (widget_class, Vmsv2Window, label);
}

static void
vmsv2_window_init (Vmsv2Window *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}
