#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "phone.h"

/**
 *  異常時に中断
 *    message : エラーメッセージ(perror()で使用)
 *    err : portaudioのエラーハンドリング用変数
 */
void die(char *message, PaError err) {   // TODO: エラーハンドリング(die()で終了させない)
  if (message) {
    perror(message);
  }
  if (gtk_main_level()) gtk_main_quit();
  done(err);
}

int max(int a, int b) {
  return a > b ? a : b;
}

int min (int a, int b) {
  return a < b ? a : b;
}

/**
 *  正の余りを求める(a%bはa<0のとき負になる)
 *  現在未使用(リングバッファで使用予定)
 *    a : 被除数
 *    b : 除数
 */
int positive_mod(int a, int b) {
  return a >= 0 ? a % b : a % b + b;
}

int validate_ip_addr(char *ip_addr) {
  struct in_addr addr;
  return inet_aton(ip_addr, &addr);
}

int validate_tcp_port(int port) {
  if (port >= 49152 && port <= 65535) return 1;
  return 0;
}

int compare_short(const void *a, const void *b) {
  return *(short *)a - *(short *)b;
}
