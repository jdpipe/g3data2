/*

g3data2 : A program for grabbing data from scanned graphs
Copyright (C) 2011 Jonas Frantz

    This file is part of g3data2.

    g3data2 is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    g3data2 is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Authors email : jonas@frantz.fi

*/

GCallback menuFileOpen(void);
GCallback menuFileExit(void);
GCallback menuHelpAbout(void);
GCallback menuTabClose(void);
GCallback toggleFullscreen(GtkWidget *widget, gpointer func_data);
GCallback hideZoomArea(GtkWidget *widget, gpointer func_data);
GCallback hideAxisSettings(GtkWidget *widget, gpointer func_data);
GCallback hideOutputProperties(GtkWidget *widget, gpointer func_data);

/* Drag and drop definitions */

static GtkTargetEntry ui_drop_target_entries[DROP_TARGET_NUM_DEFS] = {
		{"text/uri-list", GTK_TARGET_OTHER_APP, URI_LIST}
};
