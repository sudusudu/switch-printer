#include "httpd.h"
#include "webui.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
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
    (void)send(client, hdr, n, 0);
    (void)send(client, body, len, 0);
}

static void parse_method(char *req, char *method, size_t max) {
    const char *end = strchr(req, ' ');
    if (!end) { strncpy(method, "GET", max); return; }
    size_t len = end - req;
    if (len >= max) len = max - 1;
    strncpy(method, req, len);
    method[len] = '\0';
}

static void parse_path(char *req, char *path, size_t max) {
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

// Case-insensitive header lookup
static const char *find_header(const char *req, const char *name) {
    const char *hd_end = strstr(req, "\r\n\r\n");
    if (!hd_end) hd_end = req + strlen(req);
    size_t name_len = strlen(name);
    const char *p = req;
    while (p < hd_end) {
        bool match = true;
        for (size_t i = 0; i < name_len && (p + i) < hd_end; i++) {
            if (tolower((unsigned char)p[i]) != tolower((unsigned char)name[i])) {
                match = false; break;
            }
        }
        if (match && p[name_len] == ':') {
            const char *val = p + name_len + 1;
            while (*val == ' ' || *val == '\t') val++;
            return val;
        }
        const char *nl = strstr(p, "\r\n");
        if (!nl || nl >= hd_end) break;
        p = nl + 2;
    }
    return NULL;
}

static char api_buf[1024];

static void json_escape(char *dst, const char *src, size_t dst_size) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 2; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j < dst_size - 3) { dst[j++] = '\\'; dst[j++] = c; }
        } else if (c < 0x20) {
            if (j < dst_size - 1) dst[j++] = ' ';
        } else {
            dst[j++] = c;
        }
    }
    dst[j] = '\0';
}

static void handle_api(int client, Ch340Device *dev, char *method, char *path) {
    PrinterStatus st;
    gcode_get_status_safe(&st);
    const char *resp = api_buf;

    // P0: mutating ops require POST (SEC-003 CSRF)
    bool is_get = (strcmp(method, "GET") == 0);
    bool is_mutating = (strncmp(path, "/api/status", 11) != 0);
    if (is_get && is_mutating) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "Use POST method");
        send_response(client, 405, "application/json", api_buf);
        return;
    }

    if (strcmp(path, "/api/status") == 0) {
        const char *state_names[] = {"offline","idle","printing","paused","error"};
        const char *sn = (st.state < PRINTER_STATE_COUNT) ? state_names[st.state] : "unknown";
        char escaped_file[300];
        json_escape(escaped_file, st.current_file, sizeof(escaped_file));
        snprintf(api_buf, sizeof(api_buf), JSON_STATUS,
            sn, st.temp.nozzle_actual, st.temp.nozzle_target,
            st.temp.bed_actual, st.temp.bed_target,
            st.progress_percent, st.lines_sent, st.lines_total,
            escaped_file);
    }
    else if (strcmp(path, "/api/home") == 0) {
        gcode_home(dev);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "Homed");
    }
    else if (strcmp(path, "/api/pause") == 0) {
        gcode_pause();
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "Paused");
    }
    else if (strcmp(path, "/api/resume") == 0) {
        gcode_resume();
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "Resumed");
    }
    else if (strcmp(path, "/api/cancel") == 0) {
        gcode_cancel(dev);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "Cancelled");
    }
    else if (strncmp(path, "/api/move", 9) == 0) {
        float dx = 0, dy = 0, dz = 0;
        bool has_valid = false;
        char *qs = strchr(path, '?');
        if (qs) {
            char *ax = strstr(qs, "axis=");
            char *di = strstr(qs, "dist=");
            if (ax && di) {
                char axis = ax[5];
                char *endp = NULL;
                float dist = strtof(di + 5, &endp);
                if (isfinite(dist) && endp != di + 5) {
                    has_valid = true;
                    if (axis == 'x') dx = dist;
                    else if (axis == 'y') dy = dist;
                    else if (axis == 'z') dz = dist;
                    else has_valid = false;
                }
            }
        }
        if (!has_valid) {
            snprintf(api_buf, sizeof(api_buf), JSON_ERR, "Missing axis or dist");
            send_response(client, 400, "application/json", api_buf);
            return;
        }
        gcode_move(dev, dx, dy, dz, JOG_FEEDRATE);
        snprintf(api_buf, sizeof(api_buf), JSON_OK, "Moved");
    }
    else {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "Unknown API");
    }
    send_response(client, 200, "application/json", resp);
}

// P0: fixed multipart parsing (MOW-002, I-02)
static void handle_upload(int client, const char *req_body, int body_len,
                          const char *boundary_str) {
    if (body_len > HTTP_UPLOAD_MAX_MB * 1024 * 1024) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "File too large (max 100MB)");
        send_response(client, 413, "application/json", api_buf);
        return;
    }

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

    // Find file data: skip HTTP headers, then skip part headers
    const char *data_start = NULL;
    const char *http_body = strstr(req_body, "\r\n\r\n");
    if (http_body) {
        // Find part headers \r\n\r\n after boundary
        const char *after_boundary = http_body + 4;
        data_start = strstr(after_boundary, "\r\n\r\n");
        if (data_start) data_start += 4;
    }
    if (!data_start) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "Cannot parse upload");
        send_response(client, 400, "application/json", api_buf);
        return;
    }

    // Find end of file data using full boundary string
    int data_len = body_len - (int)(data_start - req_body);
    if (boundary_str && boundary_str[0]) {
        char end_marker[140];
        snprintf(end_marker, sizeof(end_marker), "\r\n--%s--", boundary_str);
        const char *file_end = strstr(data_start, end_marker);
        if (file_end) data_len = (int)(file_end - data_start);
    }

    char filepath[320];
    snprintf(filepath, sizeof(filepath), "sdmc:/switch/gcode/%s", filename);
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/gcode", 0777);
    FILE *fp = fopen(filepath, "wb");
    if (!fp) {
        snprintf(api_buf, sizeof(api_buf), JSON_ERR, "Cannot create file");
        send_response(client, 500, "application/json", api_buf);
        return;
    }
    fwrite(data_start, 1, data_len, fp);
    fclose(fp);
    gcode_start_print(g_printer, filepath);
    snprintf(api_buf, sizeof(api_buf),
        "{\"ok\":true,\"msg\":\"Uploaded: %s\",\"lines\":%d}",
        filename, data_len / GCODE_EST_BYTES_PER_LINE);
    send_response(client, 200, "application/json", api_buf);
}

static const char *extract_boundary(const char *req) {
    const char *ct = find_header(req, "Content-Type");
    if (!ct) return NULL;
    const char *b = strstr(ct, "boundary=");
    return b ? b + 9 : NULL;
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
    if (bind(g_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (g_sock >= 0) close(g_sock); g_sock = -1; return;
    }
    if (listen(g_sock, 5) < 0) {
        if (g_sock >= 0) close(g_sock); g_sock = -1; return;
    }
    struct timeval tv; tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    static char req_buf[HTTP_REQ_BUF_SIZE];

    while (g_running) {
        struct sockaddr_in ca;
        socklen_t cl = sizeof(ca);
        int client = accept(g_sock, (struct sockaddr*)&ca, &cl);
        if (client < 0) { if (!g_running) break; continue; }

        // P0: client socket recv timeout (I-03 Slowloris)
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        int recvd = recv(client, req_buf, sizeof(req_buf) - 1, 0);
        if (recvd <= 0) { close(client); continue; }
        req_buf[recvd] = '\0';

        char path[256], method[16];
        parse_path(req_buf, path, sizeof(path));
        parse_method(req_buf, method, sizeof(method));

        if (strcmp(path, "/") == 0 || strncmp(path, "/index", 6) == 0) {
            send_response(client, 200, "text/html;charset=utf-8", WEB_INDEX_HTML);
        }
        else if (strncmp(path, "/api/", 5) == 0) {
            handle_api(client, g_printer, method, path);
        }
        else if (strcmp(path, "/upload") == 0) {
            // P0: case-insensitive Content-Length with strtol (S-04, SEC-008)
            const char *cl = find_header(req_buf, "Content-Length");
            const char *hd_end = strstr(req_buf, "\r\n\r\n");
            int body_len = 0;
            if (cl) {
                char *endp = NULL;
                long val = strtol(cl, &endp, 10);
                if (val > 0 && val < HTTP_REQ_BUF_SIZE && endp != cl)
                    body_len = (int)val;
            }
            if (hd_end && body_len > 0) {
                int hdr_len = (int)(hd_end - req_buf) + 4;
                int body_got = recvd - hdr_len;
                int remain = body_len - body_got;
                if (remain > 0 && remain < (int)(sizeof(req_buf) - recvd)) {
                    int more = recv(client, req_buf + recvd, remain, 0);
                    if (more > 0) recvd += more;
                }
            }
            const char *bd_str = extract_boundary(req_buf);
            handle_upload(client, req_buf, recvd, bd_str);
        }
        else {
            snprintf(api_buf, sizeof(api_buf), JSON_ERR, "404 Not Found");
            send_response(client, 404, "application/json", api_buf);
        }
        close(client);
    }
    if (g_sock >= 0) close(g_sock);
    g_sock = -1;
}

Result httpd_init(void) { return 0; }

Result httpd_start(Ch340Device *printer_dev) {
    g_printer = printer_dev;
    g_running = true;
    Result rc = threadCreate(&g_server_thread, server_thread_func, NULL, NULL,
                             0x20000, 0x2B, -1);
    if (R_FAILED(rc)) { g_running = false; return rc; }
    threadStart(&g_server_thread);
    return 0;
}

void httpd_stop(void) {
    g_running = false;
    threadWaitForExit(&g_server_thread);
    threadClose(&g_server_thread);
}
