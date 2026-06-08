#include "httpd.h"
#include "webui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

static Ch340Device *g_printer = NULL;
static volatile bool g_running = false;
static Thread g_server_thread;
static char g_ip_str[32] = "0.0.0.0";
static int g_sock = -1;

static void update_ip(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, "wlan0", sizeof(ifr.ifr_name) - 1);
    if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
        struct sockaddr_in *sa = (struct sockaddr_in *)&ifr.ifr_addr;
        strncpy(g_ip_str, inet_ntoa(sa->sin_addr), sizeof(g_ip_str) - 1);
    }
    close(fd);
}

const char *httpd_get_ip(void) { return g_ip_str; }

static void send_response(int client, int code, const char *ctype, const char *body) {
    char hdr[512];
    int len = (int)strlen(body);
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code, code == 200 ? "OK" : "Error", ctype, len);
    send(client, hdr, n, 0);
    send(client, body, len, 0);
}

static void parse_path(const char *req, char *path, size_t max) {
    const char *start = strchr(req, ' ');
    if (!start) { strncpy(path, "/", max); return; }
    start++;
    const char *end = strchr(start, ' ');
    if (!end) { strncpy(path, "/", max); return; }
    size_t len = end - start;
    if (len >= max) len = max - 1;
    strncpy(path, start, len);
    path[len] = '\0';
}

static char api_buf[1024];

static void handle_api(int client, Ch340Device *dev, const char *path) {
    PrinterStatus st;
    gcode_get_status_safe(&st);
    const char *resp = api_buf;

    if (strcmp(path, "/api/status") == 0) {
        const char *state_names[] = {"offline","idle","printing","paused","error"};
        const char *sn = (st.state < 5) ? state_names[st.state] : "unknown";
        snprintf(api_buf, sizeof(api_buf), JSON_STATUS,
            sn, st.temp.nozzle_actual, st.temp.nozzle_target,
            st.temp.bed_actual, st.temp.bed_target,
            st.progress_percent, st.lines_sent, st.lines_total,
            st.current_file);
    }
    else if (strcmp(path, "/api/home") == 0) {
        gcode_home(dev);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "归零完成");
    }
    else if (strcmp(path, "/api/pause") == 0) {
        gcode_pause();
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "已暂停");
    }
    else if (strcmp(path, "/api/resume") == 0) {
        gcode_resume();
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "已恢复");
    }
    else if (strcmp(path, "/api/cancel") == 0) {
        gcode_cancel(dev);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "已取消");
    }
    else if (strncmp(path, "/api/move", 9) == 0) {
        float dx = 0, dy = 0, dz = 0;
        char *qs = strchr((char*)path, '?');
        if (qs) {
            char *ax = strstr(qs, "axis=");
            char *di = strstr(qs, "dist=");
            if (ax && di) {
                char axis = ax[5];
                float dist = (float)atof(di + 5);
                if (axis == 'x') dx = dist;
                if (axis == 'y') dy = dist;
                if (axis == 'z') dz = dist;
            }
        }
        gcode_move(dev, dx, dy, dz, 1000);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "已移动");
    }
    else {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "未知API");
    }
    send_response(client, 200, "application/json", resp);
}

static void handle_upload(int client, const char *req_body, int body_len) {
    const char *fn = strstr(req_body, "filename=\"");
    char filename[256] = "upload.gcode";
    if (fn) {
        fn += 10;
        const char *end = strchr(fn, '\"');
        if (end) {
            size_t flen = end - fn;
            if (flen > 250) flen = 250;
            strncpy(filename, fn, flen);
            filename[flen] = '\0';
        }
    }
    for (char *p = filename; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':') *p = '_';
    char *dot;
    while ((dot = strstr(filename, "..")) != NULL)
        memmove(dot, dot + 2, strlen(dot + 2) + 1);
    char *safe = filename;
    while (*safe == '.') safe++;
    if (safe != filename) memmove(filename, safe, strlen(safe) + 1);
    if (filename[0] == '\0') strcpy(filename, "upload.gcode");

    const char *data_start = NULL;
    for (int i = 0; i < body_len - 3; i++) {
        if (req_body[i] == '\r' && req_body[i+1] == '\n' &&
            req_body[i+2] == '\r' && req_body[i+3] == '\n') {
            data_start = req_body + i + 4;
            break;
        }
    }
    if (!data_start) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "无法解析上传文件");
        send_response(client, 400, "application/json", api_buf);
        return;
    }
    int data_len = body_len - (int)(data_start - req_body);
    const char *boundary = strstr(req_body, "boundary=");
    if (boundary) {
        char bd[128];
        strncpy(bd, boundary + 9, sizeof(bd) - 1);
        bd[sizeof(bd)-1] = '\0';
        char *end = strchr(bd, '\r');
        if (end) *end = '\0';
        for (int i = data_len - 2; i >= 0; i--) {
            if (data_start[i] == '-' && i > 0 && data_start[i-1] == '-') {
                data_len = i - 1;
                break;
            }
        }
    }

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "sdmc:/switch/gcode/%s", filename);
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/gcode", 0777);
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "无法创建文件");
        send_response(client, 500, "application/json", api_buf);
        return;
    }
    fwrite(data_start, 1, data_len, fp);
    fclose(fp);
    gcode_start_print(g_printer, filepath);
    snprintf(api_buf, sizeof(api_buf), "{\"ok\":true,\"msg\":\"已上传并开始打印: %s\",\"lines\":%d}",
             filename, data_len / 50);
    send_response(client, 200, "application/json", api_buf);
}

static void server_thread_func(void *arg) {
    (void)arg;
    update_ip();
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) return;
    int opt = 1;
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(HTTP_PORT);
    if (bind(g_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) { close(g_sock); g_sock=-1; return; }
    if (listen(g_sock, 5) < 0) { close(g_sock); g_sock=-1; return; }
    struct timeval tv; tv.tv_sec=1; tv.tv_usec=0;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char req_buf[HTTP_REQ_BUF_SIZE];

    while (g_running) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int client = accept(g_sock, (struct sockaddr*)&ca, &cl);
        if (client < 0) { if (!g_running) break; continue; }
        int recvd = recv(client, req_buf, sizeof(req_buf)-1, 0);
        if (recvd <= 0) { close(client); continue; }
        req_buf[recvd] = '\0';
        char path[256];
        parse_path(req_buf, path, sizeof(path));

        if (strcmp(path, "/")==0 || strncmp(path, "/index",6)==0) {
            send_response(client, 200, "text/html;charset=utf-8", WEB_INDEX_HTML);
        }
        else if (strncmp(path, "/api/",5)==0) {
            handle_api(client, g_printer, path);
        }
        else if (strcmp(path, "/upload") == 0) {
            const char *cl = strstr(req_buf, "Content-Length:");
            const char *hd_end = strstr(req_buf, "\r\n\r\n");
            int body_len = 0;
            if (cl) body_len = atoi(cl + 15);
            if (hd_end && body_len > 0) {
                int hdr_len = (int)(hd_end - req_buf) + 4;
                int body_got = recvd - hdr_len;
                int remain = body_len - body_got;
                if (remain > 0 && remain < (int)(sizeof(req_buf) - recvd)) {
                    int more = recv(client, req_buf + recvd, remain, 0);
                    if (more > 0) recvd += more;
                }
            }
            handle_upload(client, req_buf, recvd);
        }
        else {
            snprintf(api_buf, sizeof(api_buf), JSON_ERR, "404 未找到");
            send_response(client, 404, "application/json", api_buf);
        }
        close(client);
    }
    close(g_sock);
}

Result httpd_init(void) { return 0; }

Result httpd_start(Ch340Device *printer_dev) {
    g_printer = printer_dev;
    g_running = true;
    Result rc = threadCreate(&g_server_thread, server_thread_func, NULL, NULL, 0x10000, 0x2B, -1);
    if (R_FAILED(rc)) { g_running = false; return rc; }
    threadStart(&g_server_thread);
    return 0;
}

void httpd_stop(void) {
    g_running = false;
    threadWaitForExit(&g_server_thread);
    threadClose(&g_server_thread);
}