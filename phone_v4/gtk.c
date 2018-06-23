#include <gtk/gtk.h>
#include "phone.h"

static GtkData_t GtkData;

void add_addr(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  gtk_list_store_append(GtkData.list, &iter);
  gtk_list_store_set(GtkData.list, &iter,
                     0, DEFAULT_IP_ADDR,  // デフォルト値
                     1, DEFAULT_TCP_PORT,
                     -1);
}

void remove_addr(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, &iter)) {
    // 選択された行が見つかった場合
    gtk_list_store_remove(GtkData.list, &iter);
  }
}

void edit_ip_addr(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData.model, &iter, path)) die("get iter", paNoError);
  if (validate_ip_addr(new_text)) {
    gtk_list_store_set(GtkData.list, &iter, IP_ADDR, new_text, -1);
  } else {
    fprintf(stderr, "invalid ip address\n");
  }
}

void edit_tcp_port(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData.model, &iter, path)) die("get iter", paNoError);
  int port = atoi(new_text);
  gtk_list_store_set(GtkData.list, &iter, TCP_PORT, port, -1);
}

void enable_call(GtkTreeView *widget, gpointer data) {
  if (gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, NULL)) {
    // 選択された行がある場合
    gtk_widget_set_sensitive(GtkData.call_button, TRUE);
  } else {
    gtk_widget_set_sensitive(GtkData.call_button, FALSE);
  }
}

void enable_answer (gboolean val) {
  gtk_widget_set_sensitive(GtkData.answer_button, val);
}

void enable_hang_up (gboolean val) {
  gtk_widget_set_sensitive(GtkData.hang_up_button, val);
}

void call(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, &iter)) {
    // 選択された行が見つからなかった場合
    return;
  }

  gchar *ip_addr;
  gint tcp_port;
  gtk_tree_model_get(GtkData.model, &iter, 
                      0, &ip_addr,
                      1, &tcp_port,
                      -1);

  create_connection(ip_addr, tcp_port);
}

void answer(GtkWidget *widget, gpointer data) {
  send_ok();
}

void hang_up(GtkWidget *widget, gpointer data) {
  send_bye();
}

void quit_display(GtkWidget *widget, gpointer data) {
  gtk_widget_destroy(widget);
  SessionStatus = QUIT;
}

void prepare_to_display(int *argc, char ***argv) {
  GtkBuilder *builder;
  GtkWidget *button;
  GtkCellRenderer *renderer;

  gtk_init(argc, argv);
  builder = gtk_builder_new();
  if (!gtk_builder_add_from_file (builder, "phone.glade", NULL)) {
    perror("add from file");
    exit(1);
  }

  gtk_builder_connect_signals(builder, NULL);

  GtkData.window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
  g_signal_connect(GtkData.window, "destroy", G_CALLBACK(quit_display), NULL);

  GtkData.view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "tree_view"));
  g_signal_connect(GtkData.view, "cursor-changed", G_CALLBACK(enable_call), NULL);

  GtkData.selection = gtk_tree_view_get_selection(GtkData.view);

  button = GTK_WIDGET(gtk_builder_get_object(builder, "add_addr"));
  g_signal_connect(button, "clicked", G_CALLBACK(add_addr), NULL);

  button = GTK_WIDGET(gtk_builder_get_object(builder, "remove_addr"));
  g_signal_connect(button, "clicked", G_CALLBACK(remove_addr), NULL);

  GtkData.call_button = GTK_WIDGET(gtk_builder_get_object(builder, "call"));
  g_signal_connect(GtkData.call_button, "clicked", G_CALLBACK(call), NULL);

  GtkData.answer_button = GTK_WIDGET(gtk_builder_get_object(builder, "answer"));
  g_signal_connect(GtkData.answer_button, "clicked", G_CALLBACK(answer), NULL);

  GtkData.hang_up_button = GTK_WIDGET(gtk_builder_get_object(builder, "hang_up"));
  g_signal_connect(GtkData.hang_up_button, "clicked", G_CALLBACK(hang_up), NULL);

  renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "ip_addr_ren"));
  g_signal_connect(renderer, "edited", G_CALLBACK(edit_ip_addr), NULL);

  renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "port_ren"));
  g_signal_connect(renderer, "edited", G_CALLBACK(edit_tcp_port), NULL);

  GtkTreeIter iter;
  GtkData.list = GTK_LIST_STORE(gtk_builder_get_object(builder, "addr_list"));
  gtk_list_store_append(GtkData.list, &iter);
  gtk_list_store_set(GtkData.list, &iter,         // TODO: input from file
                      0, "192.168.1.6",
                      1, 50000,
                      -1);
  enable_call(GtkData.view, NULL);

  GtkData.model = GTK_TREE_MODEL(GtkData.list);

  g_object_unref(builder);

  gtk_widget_show(GtkData.window);
}
