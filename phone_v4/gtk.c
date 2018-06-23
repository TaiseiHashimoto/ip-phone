#include <gtk/gtk.h>
#include "phone.h"

void add_addr(GtkWidget *widget, gpointer data) {
  GtkData_t *GtkData = (GtkData_t *) data;
  GtkTreeIter iter;
  gtk_list_store_append(GtkData->list, &iter);
  gtk_list_store_set(GtkData->list, &iter,
                     0, "192.168.1.1",
                     1, 50000,
                     -1);
}

void remove_addr(GtkWidget *widget, gpointer data) {
  GtkData_t *GtkData = (GtkData_t *) data;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(GtkData->selection, &GtkData->model, &iter)) {
    // 選択された行が見つかった場合
    gtk_list_store_remove(GtkData->list, &iter);
  }
}

void edit_ip_addr(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkData_t *GtkData = (GtkData_t *) data;
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData->model, &iter, path)) die("get iter", paNoError);
  gtk_list_store_set(GtkData->list, &iter, IP_ADDR, new_text, -1);
}

void edit_tcp_port(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkData_t *GtkData = (GtkData_t *) data;
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData->model, &iter, path)) die("get iter", paNoError);
  int port = atoi(new_text);
  gtk_list_store_set(GtkData->list, &iter, TCP_PORT, port, -1);
}

void prepare_to_display(int *argc, char ***argv, GtkData_t *GtkData) {
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

  GtkData = g_slice_new(GtkData_t);

  GtkData->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));

  GtkData->view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "tree_view"));

  GtkData->selection = gtk_tree_view_get_selection(GtkData->view);

  button = GTK_WIDGET(gtk_builder_get_object(builder, "add_addr"));
  g_signal_connect(button, "clicked", G_CALLBACK(add_addr), GtkData);

  button = GTK_WIDGET(gtk_builder_get_object(builder, "remove_addr"));
  g_signal_connect(button, "clicked", G_CALLBACK(remove_addr), GtkData);

  renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "ip_addr_ren"));
  g_signal_connect(renderer, "edited", G_CALLBACK(edit_ip_addr), GtkData);

  renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "port_ren"));
  g_signal_connect(renderer, "edited", G_CALLBACK(edit_tcp_port), GtkData);

  GtkTreeIter iter;
  GtkData->list = GTK_LIST_STORE(gtk_builder_get_object(builder, "addr_list"));
  gtk_list_store_append(GtkData->list, &iter);
  gtk_list_store_set(GtkData->list, &iter,         // TODO: input from file
                      0, "192.168.1.6",
                      1, 50000,
                      -1);

  GtkData->model = GTK_TREE_MODEL(GtkData->list);

  g_object_unref(builder);

  gtk_widget_show(GtkData->window);
  gtk_main();

  g_slice_free(GtkData_t, GtkData);
}

// int main(int argc, char *argv[]) {
//   GtkBuilder *builder;
//   GtkWidget *button;
//   GtkCellRenderer *renderer;
//   GtkData_t *GtkData;

//   gtk_init(&argc, &argv);

//   builder = gtk_builder_new();
//   if (!gtk_builder_add_from_file (builder, "phone.glade", NULL)) {
//     perror("add from file");
//     exit(1);
//   }

//   gtk_builder_connect_signals(builder, NULL);

//   GtkData = g_slice_new(GtkData_t);

//   GtkData->window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));

//   GtkData->view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "tree_view"));

//   GtkData->selection = gtk_tree_view_get_selection(GtkData->view);

//   button = GTK_WIDGET(gtk_builder_get_object(builder, "add_addr"));
//   g_signal_connect(button, "clicked", G_CALLBACK(add_addr), GtkData);

//   button = GTK_WIDGET(gtk_builder_get_object(builder, "remove_addr"));
//   g_signal_connect(button, "clicked", G_CALLBACK(remove_addr), GtkData);

//   renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "ip_addr_ren"));
//   g_signal_connect(renderer, "edited", G_CALLBACK(edit_ip_addr), GtkData);

//   renderer = GTK_CELL_RENDERER(gtk_builder_get_object(builder, "port_ren"));
//   g_signal_connect(renderer, "edited", G_CALLBACK(edit_tcp_port), GtkData);

//   GtkTreeIter iter;
//   GtkData->list = GTK_LIST_STORE(gtk_builder_get_object(builder, "addr_list"));
//   gtk_list_store_append(GtkData->list, &iter);
//   gtk_list_store_set(GtkData->list, &iter,         // TODO: input from file
//                       0, "192.168.1.6",
//                       1, 50000,
//                       -1);

//   GtkData->model = GTK_TREE_MODEL(GtkData->list);

//   g_object_unref(builder);

//   gtk_widget_show(GtkData->window);
//   gtk_main();

//   g_slice_free(GtkData_t, GtkData);

//   return 0;
// }
