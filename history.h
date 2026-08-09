#ifndef G3DATA2_HISTORY_H
#define G3DATA2_HISTORY_H

#include <glib.h>

typedef struct History History;
typedef struct HistoryCommand HistoryCommand;

typedef gboolean (*HistoryApplyFunc)(gpointer context, gpointer data,
		gboolean forward, GError **error);
typedef void (*HistoryDataFreeFunc)(gpointer data);

History *history_new(gpointer context);
void history_free(History *history);

HistoryCommand *history_command_new(const gchar *label,
		HistoryApplyFunc apply, gpointer data, HistoryDataFreeFunc free_data);

gboolean history_execute(History *history, HistoryCommand *command,
		GError **error);
gboolean history_undo(History *history, GError **error);
gboolean history_redo(History *history, GError **error);

gboolean history_can_undo(const History *history);
gboolean history_can_redo(const History *history);
const gchar *history_undo_label(const History *history);
const gchar *history_redo_label(const History *history);
void history_clear(History *history);

#endif
