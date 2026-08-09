#include "history.h"

struct HistoryCommand {
	gchar *label;
	HistoryApplyFunc apply;
	gpointer data;
	HistoryDataFreeFunc free_data;
};

struct History {
	gpointer context;
	GPtrArray *undo_commands;
	GPtrArray *redo_commands;
};

static void history_command_free(gpointer data) {
	HistoryCommand *command;

	command = data;
	if (command == NULL)
		return;
	if (command->free_data != NULL)
		command->free_data(command->data);
	g_free(command->label);
	g_free(command);
}

History *history_new(gpointer context) {
	History *history;

	history = g_new0(History, 1);
	history->context = context;
	history->undo_commands = g_ptr_array_new_with_free_func(history_command_free);
	history->redo_commands = g_ptr_array_new_with_free_func(history_command_free);
	return history;
}

void history_clear(History *history) {
	if (history == NULL)
		return;
	g_ptr_array_set_size(history->undo_commands, 0);
	g_ptr_array_set_size(history->redo_commands, 0);
}

void history_free(History *history) {
	if (history == NULL)
		return;
	g_ptr_array_free(history->undo_commands, TRUE);
	g_ptr_array_free(history->redo_commands, TRUE);
	g_free(history);
}

HistoryCommand *history_command_new(const gchar *label,
		HistoryApplyFunc apply, gpointer data, HistoryDataFreeFunc free_data) {
	HistoryCommand *command;

	g_return_val_if_fail(apply != NULL, NULL);
	command = g_new0(HistoryCommand, 1);
	command->label = g_strdup(label != NULL ? label : "Change");
	command->apply = apply;
	command->data = data;
	command->free_data = free_data;
	return command;
}

static HistoryCommand *history_pop(GPtrArray *commands) {
	if (commands == NULL || commands->len == 0)
		return NULL;
	return g_ptr_array_steal_index(commands, commands->len - 1);
}

gboolean history_execute(History *history, HistoryCommand *command,
		GError **error) {
	g_return_val_if_fail(history != NULL, FALSE);
	g_return_val_if_fail(command != NULL, FALSE);
	if (!command->apply(history->context, command->data, TRUE, error)) {
		history_command_free(command);
		return FALSE;
	}
	g_ptr_array_set_size(history->redo_commands, 0);
	g_ptr_array_add(history->undo_commands, command);
	return TRUE;
}

gboolean history_undo(History *history, GError **error) {
	HistoryCommand *command;

	g_return_val_if_fail(history != NULL, FALSE);
	command = history_pop(history->undo_commands);
	if (command == NULL)
		return FALSE;
	if (!command->apply(history->context, command->data, FALSE, error)) {
		g_ptr_array_add(history->undo_commands, command);
		return FALSE;
	}
	g_ptr_array_add(history->redo_commands, command);
	return TRUE;
}

gboolean history_redo(History *history, GError **error) {
	HistoryCommand *command;

	g_return_val_if_fail(history != NULL, FALSE);
	command = history_pop(history->redo_commands);
	if (command == NULL)
		return FALSE;
	if (!command->apply(history->context, command->data, TRUE, error)) {
		g_ptr_array_add(history->redo_commands, command);
		return FALSE;
	}
	g_ptr_array_add(history->undo_commands, command);
	return TRUE;
}

gboolean history_can_undo(const History *history) {
	return history != NULL && history->undo_commands->len > 0;
}

gboolean history_can_redo(const History *history) {
	return history != NULL && history->redo_commands->len > 0;
}

const gchar *history_undo_label(const History *history) {
	HistoryCommand *command;

	if (!history_can_undo(history))
		return NULL;
	command = g_ptr_array_index(history->undo_commands,
			history->undo_commands->len - 1);
	return command->label;
}

const gchar *history_redo_label(const History *history) {
	HistoryCommand *command;

	if (!history_can_redo(history))
		return NULL;
	command = g_ptr_array_index(history->redo_commands,
			history->redo_commands->len - 1);
	return command->label;
}
