#ifndef _PHONE_H_
#define _PHONE_H_

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/udp.h>
#include <portaudio.h>
#include <gtk/gtk.h>

#define SAMPLE_RATE   8000    // サンプル周波数(Hz)
#define SOUND_BUF     8000    // 音声データの受信用バッファ長
#define CHAR_BUF      200     // セッション管理用文字列のバッファ長
#define QUE_LEN       10      // 保留中の接続のキューの最大長
#define SILENT        100     // 無音とみなす音量の閾値  TODO: 自動で調整
#define SILENT_FRAMES 8000    // 沈黙とみなす、無音の最小連続フレーム数

enum Status {
  NO_SESSION,     // セッションを開始していない
  NEGOTIATING,    // セッションの確立中
  INVITING,       // 呼び出し中
  RINGING,        // コール受信中
  SPEAKING        // 通話中
};

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

typedef struct {
  fd_set rfds;
  fd_set rfds_org;
  int max_fd;
} MonitorData_t;

typedef struct {
  int sock;
  struct sockaddr_in *addr;
  int silent_frames;    // 無音が連続しているフレーム数
} RecAndSendData_t;


#define GLADE_FILE "phone.glade"

typedef struct {        // Gtk関連のデータを保持する構造体
  GtkWidget *window;            // メインウィンドウ
  GtkTreeView *view;            // ツリービュー(表のビュー)
  GtkListStore *list;           // ツリーのデータモデル
  GtkTreeModel *model;          // GtkListStoreのインターフェース
  GtkTreeSelection *selection;  // ツリービューの選択について制御する
} GtkData_t;

enum Columns {
  IP_ADDR,
  TCP_PORT
};

/* util.c */
void die(char *message, PaError err);
int max(int a, int b);
int min (int a, int b);
int positive_mod(int a, int b);

/* portaudio.c */
int done(PaError err);
int rec_and_send(const void *inputBuffer, void *outputBuffer,
                        unsigned long framesPerBuffer,
                        const PaStreamCallbackTimeInfo *timeInfo,
                        PaStreamCallbackFlags statusFlags,
                        void *userData);
void open_rec(PaStream **stream, RecAndSendData_t *RecAndSendData);

/* gtk.c */
void add_addr(GtkWidget *widget, gpointer data);
void remove_addr(GtkWidget *widget, gpointer data);
void edit_ip_addr(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data);
void edit_tcp_port(GtkCellRendererText *widget, gchar *path, gchar *new_text, gpointer data);
void prepare_to_display(int *argc, char ***argv, GtkData_t *GtkData);

/* event.c */
int create_connection(InetData_t *InetData, MonitorData_t *MonitorData, char *str);
int accept_connection(InetData_t *InetData, MonitorData_t *MonitorData);
int recv_invitation(InetData_t *InetData, MonitorData_t *MonitorData);
int send_ok(InetData_t *InetData, MonitorData_t *MonitorData, PaStream **audioStream, RecAndSendData_t *RecAndSendData);
int recv_ok(InetData_t *InetData, MonitorData_t *MonitorData, PaStream **audioStream, RecAndSendData_t *RecAndSendData);
int recv_and_play(InetData_t *InetData);
int stop_speaking(InetData_t *InetData, PaStream *audioStream);

#endif  // _PHONE_H_
