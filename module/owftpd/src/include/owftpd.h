/*
 * $Id$
 */

#ifndef OWFTPD_H
#define OWFTPD_H

#include <config.h>
#include <owfs_config.h>
#include <ow.h>
#include <ow_connection.h>

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif
#include <sys/socket.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

/* _x_ must be a pointer to a sockaddr structure */

#define SAFAM(_x_)  (((struct sockaddr *)(_x_))->sa_family)

#ifdef HAVE_NEW_SS_FAMILY
#define SSFAM(_x_)  (((struct sockaddr_storage *)(_x_))->ss_family)
#else
#define SSFAM(_x_)  (((struct sockaddr_storage *)(_x_))->__ss_family)
#endif

#define SIN4ADDR(_x_)   (((struct sockaddr_in *)(_x_))->sin_addr)
#define SIN4PORT(_x_)   (((struct sockaddr_in *)(_x_))->sin_port)
#define SIN6ADDR(_x_)   (((struct sockaddr_in6 *)(_x_))->sin6_addr)
#define SIN6PORT(_x_)   (((struct sockaddr_in6 *)(_x_))->sin6_port)

#ifdef INET6
#define SINADDR(_x_)    ((SAFAM(_x_)==AF_INET6) ? SIN6ADDR(_x_) : SIN4ADDR(_x_))
#define SINPORT(_x_)    ((SAFAM(_x_)==AF_INET6) ? SIN6PORT(_x_) : SIN4PORT(_x_))
#else
#define SINADDR(_x_)    SIN4ADDR(_x_)
#define SINPORT(_x_)    SIN4PORT(_x_)
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

#ifdef INET6
#define IP6_ADDRSTRLEN INET6_ADDRSTRLEN
#define IP4_ADDRSTRLEN INET_ADDRSTRLEN
#define IP_ADDRSTRLEN INET6_ADDRSTRLEN
#else
#define IP_ADDRSTRLEN INET_ADDRSTRLEN
#endif

#ifdef INET6
typedef struct sockaddr_storage sockaddr_storage_t;
#else
typedef struct sockaddr_in sockaddr_storage_t;
#endif

/* address to listen on (use NULL to listen on all addresses) */
#define FTP_ADDRESS NULL

/* default port FTP server listens on (use 0 to listen on default port) */
#define FTP_PORT 0

/* ranges possible for command-line specified port numbers */
#define MIN_PORT 0
#define MAX_PORT 65535

/* default port FTP server listens on (use 0 to listen on default port) */
#define DEFAULT_PORTNAME "0.0.0.0:21"

/* default maximum number of clients */
#define MAX_CLIENTS 250

/* bounds on command-line specified number of clients */
#define MIN_NUM_CLIENTS 1
#define MAX_NUM_CLIENTS 300

/* timeout (in seconds) before dropping inactive clients */
#define INACTIVITY_TIMEOUT (15 * 60)

/* README file name (sent automatically as a response to users) */
#define README_FILE_NAME "README"

struct error_code_s {
    int error_code;
    char desc[128];
} ;

#if TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

//#include "ftp_error.h"

/* each watched thread gets one of these structures */
struct watched_s {
    /* thread to monitor */
    pthread_t watched_thread;

    /* flag whether in a watchdog list */
    int in_list;

    /* time when to cancel thread if no activity */
    time_t alarm_time;

    /* for location in doubly-linked list */
    struct watched_s *older;
    struct watched_s *newer;

    /* watchdog that this watched_s is in */
    void *watchdog;
} ;

/* the watchdog keeps track of all information */
struct watchdog_s {
    pthread_mutex_t mutex;
    int inactivity_timeout;

    /* the head and tail of our list */
    struct watched_s *oldest;
    struct watched_s *newest;
} ;

int watchdog_init(struct watchdog_s *w, int inactivity_timeout, struct error_code_s *err);
void watchdog_add_watched(struct watchdog_s *w, struct watched_s *watched);
void watchdog_defer_watched( struct watched_s *watched);
void watchdog_remove_watched(struct watched_s *watched);


/* size of buffer */
#define BUF_LEN 2048

/* information on a telnet session */
struct telnet_session_s {
    int in_fd;
    int in_errno;
    int in_eof;
    int in_take;
    int in_add;
    char in_buf[BUF_LEN];
    int in_buflen;

    int in_status;

    int out_fd;
    int out_errno;
    int out_eof;
    int out_take;
    int out_add;
    char out_buf[BUF_LEN];
    int out_buflen;
} ;

void ow_exit( int e ) ;

#ifdef NDEBUG

 #define daemon_assert(expr)

#else /* NDEBUG */

void daemon_assert_fail(const char *assertion,
                        const char *file,
                        int line,
                        const char *function);

 #ifndef __STRING
  #define __STRING(x) #x
 #endif

 #define daemon_assert(expr)                                                   \
           ((expr) ? 0 :                                                      \
            (daemon_assert_fail(__STRING(expr), __FILE__, __LINE__, __func__)))

#endif /* NDEBUG */

/* data representation types supported */
#define TYPE_ASCII  0
#define TYPE_IMAGE  1

/* file structure types supported */
#define STRU_FILE   0
#define STRU_RECORD 1

/* data path chosen */
#define DATA_PORT     0
#define DATA_PASSIVE  1

/* space required for text representation of address and port,
   e.g. "192.168.0.1 port 1024" or
        "2001:3333:DEAD:BEEF:0666:0013:0069:0042 port 65535" */
#define ADDRPORT_STRLEN 58

/* structure encapsulating an FTP session's information */
struct ftp_session_s {
    /* flag whether session is active */
    int session_active;

    /* incremented for each command */
    unsigned long command_number;

    /* options about transfer set by user */
    int data_type;
    int file_structure;

    /* offset to begin sending file from */
    off_t file_offset;
    unsigned long file_offset_command_number;

    /* flag set if client requests ESPV ALL - this prevents subsequent
    use of PORT, PASV, LPRT, LPSV, or EPRT */
    int epsv_all_set;

    /* address of client */
    sockaddr_storage_t client_addr;
    char client_addr_str[ADDRPORT_STRLEN];

    /* address of server (including IPv4 version) */
    sockaddr_storage_t server_addr;
    struct sockaddr_in server_ipv4_addr;

    /* telnet session to encapsulate control channel logic */
    struct telnet_session_s *telnet_session;

    /* current working directory of this connection */
    char dir[PATH_MAX+1];

    /* data channel information, including type,
    and client address or server port depending on type */
    int data_channel;
    sockaddr_storage_t data_port;
    int server_fd;

    /* watchdog to handle timeout */
    struct watched_s *watched;
} ;

int ftp_session_init(struct ftp_session_s *f,
                     const sockaddr_storage_t *client_addr,
                     const sockaddr_storage_t *server_addr,
                     struct telnet_session_s *t,
                     const char *dir,
                     struct error_code_s *err);
void ftp_session_drop(struct ftp_session_s *f, const char *reason);
void ftp_session_run(struct ftp_session_s *f, struct watched_s *watched);
void ftp_session_destroy(struct ftp_session_s *f);
#define DEFAULT_FTP_PORT 21

struct ftp_listener_s {

    /* file descriptor incoming connections arrive on */
    int fd;

    /* maximum number of connections */
    int max_connections;

    /* current number of connections */
    int num_connections;

    /* timeout (in seconds) for connections */
    int inactivity_timeout;

    /* watchdog monitoring this listener's connections */
    struct watchdog_s watchdog;

    /* mutext to lock changes to this structure */
    pthread_mutex_t mutex;

    /* starting directory */
    char dir[PATH_MAX+1];

    /* boolean defining whether listener is running or not */
    int listener_running;

    /* thread identifier for listener */
    pthread_t listener_thread;

    /* end of pipe to wake up listening thread with */
    int shutdown_request_send_fd;

    /* end of pipe listening thread waits on */
    int shutdown_request_recv_fd;

    /* condition to signal thread requesting shutdown */
    pthread_cond_t shutdown_cond;

} ;

int ftp_listener_init(struct ftp_listener_s *f, char *address, int port,
    int max_connections, int inactivity_timeout, struct error_code_s *err);
int ftp_listener_start(struct ftp_listener_s *f, struct error_code_s *err);
void ftp_listener_stop(struct ftp_listener_s *f);

/* special macro for handling EPSV ALL requests */
#define EPSV_ALL (-1)

/* maximum possible number of arguments */
#define MAX_ARG 2

/* maximum string length */
#define MAX_STRING_LEN PATH_MAX

struct ftp_command_s {
    char command[5];
    int num_arg;
    union {
        char string[MAX_STRING_LEN+1];
        sockaddr_storage_t host_port;
        int num;
        off_t offset;
    } arg[MAX_ARG];
} ;

/* methods */
void error_init(struct error_code_s *err, int error_code, const char *desc_fmt, ...);
int error_get_error_code(const struct error_code_s *err);
const char *error_get_desc(const struct error_code_s *err);
int file_nlst(int out, const char *cur_dir, const char *filespec);
int file_list(int out, const char *cur_dir, const char *filespec);
int ftp_command_parse(const char *input, struct ftp_command_s *cmd);

/* functions */
void telnet_session_init(struct telnet_session_s *t, int in, int out);
int telnet_session_print(struct telnet_session_s *t, const char *s);
int telnet_session_println(struct telnet_session_s *t, const char *s);
int telnet_session_readln(struct telnet_session_s *t, char *buf, int buflen);
void telnet_session_destroy(struct telnet_session_s *t);

#endif /* OWFTPD_H */

