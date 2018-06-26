#ifndef _PHONE_H_
#define _PHONE_H_

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <portaudio.h>
#include <gtk/gtk.h>
#include <stdlib.h>

#define SAMPLE_RATE     8000    // サンプル周波数(Hz)
#define SOUND_BUF       16000    // 音声データのバッファ長
#define CHAR_BUF        200     // セッション管理用文字列のバッファ長
#define QUE_LEN         10      // 保留中の接続のキューの最大長

#define DEFAULT_SILENT  500     // 無音とみなす音量の閾値  TODO: 自動で調整
#define SILENT_FRAMES   4000    // 沈黙とみなす、無音の最小連続フレーム数
#define BLOCK_LEN       30
#define CONVERGE        10
#define SIGNIFICANT     3

enum Status {
  NO_SESSION,     // セッションを開始していない
  NEGOTIATING,    // セッションの確立中
  INVITING,       // 呼び出し中
  RINGING,        // コール受信中
  SPEAKING,       // 通話中
  QUIT            // アプリケーション全体を終了
};
extern enum Status SessionStatus;

// UDPでの送信用の情報を持つ構造体(rec_and_send()で使用)
typedef struct {
  int tcp_sock;
  int tcp_s;
  int udp_sock;               // ソケットID
  struct sockaddr_in my_tcp_addr;   // 送信相手アドレス構造体
  struct sockaddr_in my_udp_addr;
  struct sockaddr_in ot_tcp_addr;
  struct sockaddr_in ot_udp_addr;
  char *ot_ip_addr;
  int my_tcp_port;
  int my_udp_port;
  int ot_tcp_port;
  int ot_udp_port;
} InetData_t;
extern InetData_t InetData;

typedef struct {
  GIOChannel *g_tcp_sock;
  GIOChannel *g_tcp_s;
  GIOChannel *g_udp_sock;
  guint tcp_sock_tag;
  guint tcp_s_tag;
  guint udp_sock_tag;
} MonitorData_t;
extern MonitorData_t MonitorData;


typedef struct {
  int silent_frames;
  int check_count;            // ノイズ音量チェックするかどうか
  short sbuf[SAMPLE_RATE*2];  // 2秒分(暫定的)
} RecAndSendData_t;

#define GLADE_FILE "phone.glade"

typedef struct {        // Gtk関連のデータを保持する構造体
  GtkWidget *window;            // メインウィンドウ
  GtkWidget *main_page;
  GtkWidget *inviting_page;      // 発信中の画面
  GtkTextBuffer *inviting_tb;
  GtkWidget *ringing_page;      // 受信中の画面
  GtkTextBuffer *ringing_tb;
  GtkWidget *speaking_page;     // 通話中の画面
  GtkTextBuffer *speaking_tb;
  GtkTreeView *view;            // ツリービュー(表のビュー)
  GtkListStore *list;           // ツリーのデータモデル
  GtkTreeModel *model;          // GtkListStoreのインターフェース
  GtkTreeSelection *selection;  // ツリービューの選択について制御する
  GtkWidget *call_button;
  GtkWidget *answer_button;
  GtkWidget *hang_up_button;
} GtkData_t;

enum Columns {
  IP_ADDR,
  TCP_PORT
};

#define DEFAULT_IP_ADDR "192.168.1.1"
#define DEFAULT_TCP_PORT 50000

/* util.c */
void die(char *message, PaError err);
int max(int a, int b);
int min (int a, int b);
int positive_mod(int a, int b);
int validate_ip_addr(char *ip_addr);
int validate_tcp_port(int port);
int compare_short(const void *a, const void *b);

/* audio.c */
int done(PaError err);
void open_rec(PaStream **audioStream);
void open_play(PaStream **outStream);
int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData);
gboolean recv_and_play(GIOChannel *s, GIOCondition c, gpointer d);

/* gtk.c */
void add_addr(GtkWidget *widget, gpointer data);
void remove_addr(GtkWidget *widget, gpointer data);
void edit_ip_addr(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data);
void edit_tcp_port(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data);
void enable_call(GtkTreeView *widget, gpointer data);
void call(GtkWidget *widget, gpointer data);
void cancel(GtkWidget *widget, gpointer data);
void canceled();
void answer(GtkWidget *widget, gpointer data);
void decline(GtkWidget *widget, gpointer data);
void declined();
void hang_up(GtkWidget *widget, gpointer data);
void quit_display(GtkWidget *widget, gpointer data);
void prepare_to_display(int *argc, char ***argv);
void ringing();
void speaking();
void stop_speaking();

/* event.c */
void create_connection(char *ot_ip_addr, int ot_tcp_port);
gboolean accept_connection(GIOChannel *s, GIOCondition c, gpointer d);
void recv_invitation();
void send_cancel();
void recv_cancel();
void send_ok();
void send_ng();
void recv_ok_ng();
void send_bye();
void recv_bye();
gboolean assign_task(GIOChannel *s, GIOCondition c, gpointer d);

#endif  // _PHONE_H_
