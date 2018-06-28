#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdio.h>
#include <string.h>
#include "phone.h"

static GtkData_t GtkData;

void show_volume(short volume) {
  double fraction = (double)volume / MAX_VOLUME;
  if (fraction > 1) fraction = 1;
  gtk_progress_bar_set_fraction(GtkData.volume_bar, fraction);
}

void collect_addresses() {
  int count = 0;
  GtkTreeIter iter;
  gboolean result;
  result = gtk_tree_model_get_iter_first(GtkData.model, &iter);
  if (!result) {
    save_address(NULL, NULL, 0);  // ファイルを空にする
    return;
  }
  while(result) {
    count++;
    result = gtk_tree_model_iter_next(GtkData.model, &iter);
  }

  gchararray *ip_addrs = (gchararray *) malloc(sizeof(gchararray) * count);
  gint *tcp_ports = (gint *) malloc(sizeof(gint) * count);

  result = gtk_tree_model_get_iter_first(GtkData.model, &iter);
  if (!result) return;
  int i = 0;
  while (result) {
    gtk_tree_model_get(GtkData.model, &iter, 
                      IP_ADDR, ip_addrs + i,
                      TCP_PORT, tcp_ports + i,
                      -1);
    i++;
    result = gtk_tree_model_iter_next(GtkData.model, &iter);
  }

  save_address(ip_addrs, tcp_ports, count);

  for (int i = 0; i < count; i++) free(ip_addrs[i]);
  free(ip_addrs);
  free(tcp_ports);
}

G_MODULE_EXPORT
void add_addr(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  gtk_list_store_append(GtkData.list, &iter);
  gtk_list_store_set(GtkData.list, &iter,
                     IP_ADDR, DEFAULT_IP_ADDR,  // デフォルト値
                     TCP_PORT, DEFAULT_TCP_PORT,
                     -1);
  collect_addresses();
}

G_MODULE_EXPORT
void remove_addr(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, &iter)) {
    // 選択された行が見つかった場合
    gtk_list_store_remove(GtkData.list, &iter);
  }
  collect_addresses();
}

G_MODULE_EXPORT
void edit_ip_addr(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData.model, &iter, path)) die("get iter", paNoError);
  if (validate_ip_addr(new_text)) {
    gtk_list_store_set(GtkData.list, &iter, IP_ADDR, new_text, -1);
  } else {
    fprintf(stderr, "invalid ip address\n");
  }

  collect_addresses();
}

G_MODULE_EXPORT
void edit_tcp_port(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_model_get_iter_from_string(GtkData.model, &iter, path)) die("get iter", paNoError);
  int port = atoi(new_text);
  if (validate_tcp_port(port)) {
    gtk_list_store_set(GtkData.list, &iter, TCP_PORT, port, -1);
  } else {
    fprintf(stderr, "invalid port number\n");
  }

  collect_addresses();
}

G_MODULE_EXPORT
void enable_call(GtkTreeView *widget, gpointer data) {
  if (gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, NULL)) {
    // 選択された行がある場合
    gtk_widget_set_sensitive(GtkData.call_button, TRUE);
  } else {
    gtk_widget_set_sensitive(GtkData.call_button, FALSE);
  }
}

G_MODULE_EXPORT
void call(GtkWidget *widget, gpointer data) {
  GtkTreeIter iter;
  if (!gtk_tree_selection_get_selected(GtkData.selection, &GtkData.model, &iter)) {
    // 選択された行が見つからなかった場合
    return;
  }

  gchar *ip_addr;
  gint tcp_port;
  gtk_tree_model_get(GtkData.model, &iter, 
                      IP_ADDR, &ip_addr,
                      TCP_PORT, &tcp_port,
                      -1);

  create_connection(ip_addr, tcp_port);

  gchar cbuf[CHAR_BUF];
  sprintf(cbuf, "Inviting\n%s", InetData.ot_ip_addr);
  gtk_text_buffer_set_text(GtkData.inviting_tb, cbuf, strlen(cbuf));

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.main_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.inviting_page);

  GtkTextTag *tag = gtk_text_buffer_create_tag (GtkData.inviting_tb, NULL,
                        "font", "30", 
                        "background", "#fafafa",
                        NULL);
  GtkTextIter start, end;
  gtk_text_buffer_get_start_iter(GtkData.inviting_tb, &start);
  gtk_text_buffer_get_end_iter(GtkData.inviting_tb, &end);
  gtk_text_buffer_apply_tag (GtkData.inviting_tb, tag, &start, &end);

  free(ip_addr);
}

G_MODULE_EXPORT
void cancel(GtkWidget *widget, gpointer data) {
  send_cancel();

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.inviting_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

void canceled() {
  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.ringing_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

void ringing() {
  gchar cbuf[CHAR_BUF];
  sprintf(cbuf, "Call from\n%s", InetData.ot_ip_addr);
  gtk_text_buffer_set_text(GtkData.ringing_tb, cbuf, strlen(cbuf));

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.main_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.ringing_page);

  GtkTextTag *tag = gtk_text_buffer_create_tag (GtkData.ringing_tb, NULL,
                        "font", "30", 
                        "background", "#fafafa",
                        NULL);
  GtkTextIter start, end;
  gtk_text_buffer_get_start_iter(GtkData.ringing_tb, &start);
  gtk_text_buffer_get_end_iter(GtkData.ringing_tb, &end);
  gtk_text_buffer_apply_tag (GtkData.ringing_tb, tag, &start, &end);
}

G_MODULE_EXPORT
void answer(GtkWidget *widget, gpointer data) {
  send_ok();

  gchar cbuf[CHAR_BUF];
  sprintf(cbuf, "Talking with\n%s", InetData.ot_ip_addr);
  gtk_text_buffer_set_text(GtkData.speaking_tb, cbuf, strlen(cbuf));

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.ringing_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.speaking_page);

  GtkTextTag *tag = gtk_text_buffer_create_tag (GtkData.speaking_tb, NULL,
                        "font", "30", 
                        "background", "#fafafa",
                        NULL);
  GtkTextIter start, end;
  gtk_text_buffer_get_start_iter(GtkData.speaking_tb, &start);
  gtk_text_buffer_get_end_iter(GtkData.speaking_tb, &end);
  gtk_text_buffer_apply_tag (GtkData.speaking_tb, tag, &start, &end);
}

G_MODULE_EXPORT
void decline(GtkWidget *widget, gpointer data) {
  send_ng();

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.ringing_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

void declined() {
  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.inviting_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

void speaking() {
  gchar cbuf[CHAR_BUF];
  sprintf(cbuf, "Talking with\n%s", InetData.ot_ip_addr);
  gtk_text_buffer_set_text(GtkData.speaking_tb, cbuf, strlen(cbuf));

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.inviting_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.speaking_page);

  GtkTextTag *tag = gtk_text_buffer_create_tag (GtkData.speaking_tb, NULL,
                        "font", "30", 
                        "background", "#fafafa",
                        NULL);
  GtkTextIter start, end;
  gtk_text_buffer_get_start_iter(GtkData.speaking_tb, &start);
  gtk_text_buffer_get_end_iter(GtkData.speaking_tb, &end);
  gtk_text_buffer_apply_tag (GtkData.speaking_tb, tag, &start, &end);
}

void stop_speaking() {
  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.speaking_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

G_MODULE_EXPORT
void hang_up(GtkWidget *widget, gpointer data) {
  send_bye();

  gtk_container_remove(GTK_CONTAINER(GtkData.window), GtkData.speaking_page);
  gtk_container_add(GTK_CONTAINER(GtkData.window), GtkData.main_page);
}

G_MODULE_EXPORT
void quit_display(GtkWidget *widget, gpointer data) {
  // gtk_widget_destroy(widget);
  gtk_main_quit();
  g_object_unref(GtkData.inviting_page);
  g_object_unref(GtkData.ringing_page);
  g_object_unref(GtkData.speaking_page);

  SessionStatus = QUIT;
}

void prepare_to_display(int *argc, char ***argv) {
  GtkBuilder *builder;

  gtk_init(argc, argv);
  builder = gtk_builder_new();    // gladeファイルからの読み込み用
  if (!gtk_builder_add_from_file (builder, "glade/phone.glade", NULL)) {
    perror("add from file");
    exit(1);
  }

  gtk_builder_connect_signals(builder, NULL);   // シグナルを接続

  // CSSの設定
  GtkCssProvider *provider = gtk_css_provider_get_default();
  gtk_css_provider_load_from_path(provider, "css/phone.css", NULL);
  GdkScreen *screen = gdk_screen_get_default();
  gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_USER);

  GtkData.window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
  GtkData.main_page = GTK_WIDGET(gtk_builder_get_object(builder, "main_page"));
  GtkData.inviting_page = GTK_WIDGET(gtk_builder_get_object(builder, "inviting_page"));
  GtkData.ringing_page = GTK_WIDGET(gtk_builder_get_object(builder, "ringing_page"));
  GtkData.speaking_page = GTK_WIDGET(gtk_builder_get_object(builder, "speaking_page"));

  GtkData.ringing_tb = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "ringing_tb"));
  GtkData.inviting_tb = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "inviting_tb"));
  GtkData.speaking_tb = GTK_TEXT_BUFFER(gtk_builder_get_object(builder, "speaking_tb"));

  g_object_ref(GtkData.main_page);
  g_object_ref(GtkData.inviting_page);
  g_object_ref(GtkData.ringing_page);
  g_object_ref(GtkData.speaking_page);

  GtkData.view = GTK_TREE_VIEW(gtk_builder_get_object(builder, "tree_view"));
  GtkData.selection = gtk_tree_view_get_selection(GtkData.view);

  GtkData.call_button = GTK_WIDGET(gtk_builder_get_object(builder, "call_button"));
  GtkData.answer_button = GTK_WIDGET(gtk_builder_get_object(builder, "answer_button"));
  GtkData.hang_up_button = GTK_WIDGET(gtk_builder_get_object(builder, "hang_up_button"));

  // アドレスをファイルから読み込む
  char **ip_addrs = NULL;
  int *tcp_ports = NULL;
  int count = retrieve_address(&ip_addrs, &tcp_ports);
  // リストに追加
  GtkTreeIter iter;
  GtkData.list = GTK_LIST_STORE(gtk_builder_get_object(builder, "addr_list"));
  for (int i = 0; i < count; i++) {
    gtk_list_store_append(GtkData.list, &iter);
    gtk_list_store_set(GtkData.list, &iter,
                        IP_ADDR, ip_addrs[i],
                        TCP_PORT, tcp_ports[i],
                        -1);
  }
  for (int i = 0; i < count; i++) free(ip_addrs[i]);
  free(ip_addrs);
  free(tcp_ports);
  

  GtkData.model = GTK_TREE_MODEL(GtkData.list);

  GtkData.volume_bar = GTK_PROGRESS_BAR(gtk_builder_get_object(builder, "volume_bar"));


  g_object_unref(builder);
  g_object_unref(provider);

  enable_call(GtkData.view, NULL);
  show_volume(DEFAULT_SILENT);

  gtk_widget_show(GtkData.window);
}
