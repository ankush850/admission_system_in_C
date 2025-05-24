#define _WIN32_WINNT 0x0601  // Windows 7 or later for getaddrinfo
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma comment(lib, "Ws2_32.lib")

#define PORT "8080"
#define BACKLOG 10
#define BUFFER_SIZE 8192

#define DATA_FILE "data\\students.dat"
#define PUBLIC_DIR "public\\"

struct Student {
    int id;
    char name[50];
    char course[50];
};

void url_decode(const char *src, char *dest) {
    while (*src) {
        if (*src == '%') {
            if (src[1] && src[2]) {
                char hex[3] = { src[1], src[2], 0 };
                *dest++ = (char)strtol(hex, NULL, 16);
                src += 3;
            }
            else src++;
        }
        else if (*src == '+') {
            *dest++ = ' ';
            src++;
        }
        else {
            *dest++ = *src++;
        }
    }
    *dest = 0;
}

char *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(fp);
        return NULL;
    }
    fread(buffer, 1, size, fp);
    fclose(fp);
    buffer[size] = 0;
    if (out_len) *out_len = size;
    return buffer;
}

void send_response(SOCKET client, const char *status, const char *content_type, const char *body) {
    char header[BUFFER_SIZE];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\n"
             "Connection: close\r\n"
             "\r\n",
             status, content_type, strlen(body));
    send(client, header, strlen(header), 0);
    send(client, body, strlen(body), 0);
}

void send_404(SOCKET client) {
    send_response(client, "404 Not Found", "text/html", "<h1>404 Not Found</h1>");
}

void send_400(SOCKET client) {
    send_response(client, "400 Bad Request", "text/html", "<h1>400 Bad Request</h1>");
}

int ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix > lenstr) return 0;
    return strcmp(str + lenstr - lensuffix, suffix) == 0;
}

void serve_static(SOCKET client, const char *path) {
    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", PUBLIC_DIR, path);

    size_t file_len;
    char *file_content = read_file(full_path, &file_len);
    if (!file_content) {
        send_404(client);
        return;
    }

    const char *content_type = "text/plain";
    if (ends_with(path, ".html")) content_type = "text/html";
    else if (ends_with(path, ".css")) content_type = "text/css";
    else if (ends_with(path, ".js")) content_type = "application/javascript";
    else if (ends_with(path, ".json")) content_type = "application/json";

    send_response(client, "200 OK", content_type, file_content);
    free(file_content);
}

int add_student(const struct Student *s) {
    FILE *fp = fopen(DATA_FILE, "ab");
    if (!fp) return 0;
    fwrite(s, sizeof(*s), 1, fp);
    fclose(fp);
    return 1;
}

char *get_all_students_json() {
    FILE *fp = fopen(DATA_FILE, "rb");
    if (!fp) {
        return strdup("[]");
    }

    struct Student s;
    size_t capacity = 1024;
    char *json = malloc(capacity);
    if (!json) {
        fclose(fp);
        return strdup("[]");
    }

    strcpy(json, "[");
    size_t len = 1;
    int first = 1;
    while (fread(&s, sizeof(s), 1, fp) == 1) {
        char student_json[256];
        snprintf(student_json, sizeof(student_json),
            "%s{\"id\":%d,\"name\":\"%s\",\"course\":\"%s\"}",
            first ? "" : ",",
            s.id, s.name, s.course);

        size_t needed = len + strlen(student_json) + 2;
        if (needed > capacity) {
            capacity *= 2;
            char *tmp = realloc(json, capacity);
            if (!tmp) {
                free(json);
                fclose(fp);
                return strdup("[]");
            }
            json = tmp;
        }
        strcat(json, student_json);
        len = strlen(json);
        first = 0;
    }
    fclose(fp);
    strcat(json, "]");
    return json;
}

int parse_post_data(const char *data, struct Student *s) {
    char *data_copy = _strdup(data);
    if (!data_copy) return 0;

    char *token = strtok(data_copy, "&");
    int fields_set = 0;
    while (token) {
        char key[50], val[50];
        if (sscanf(token, "%49[^=]=%49s", key, val) == 2) {
            char decoded[50];
            url_decode(val, decoded);
            if (strcmp(key, "id") == 0) {
                s->id = atoi(decoded);
                fields_set++;
            }
            else if (strcmp(key, "name") == 0) {
                strncpy(s->name, decoded, sizeof(s->name));
                s->name[sizeof(s->name) - 1] = 0;
                fields_set++;
            }
            else if (strcmp(key, "course") == 0) {
                strncpy(s->course, decoded, sizeof(s->course));
                s->course[sizeof(s->course) - 1] = 0;
                fields_set++;
            }
        }
        token = strtok(NULL, "&");
    }
    free(data_copy);
    return fields_set == 3;
}

void handle_client(SOCKET client) {
    char buffer[BUFFER_SIZE];
    int received = recv(client, buffer, sizeof(buffer) - 1, 0);
    if (received <= 0) {
        closesocket(client);
        return;
    }
    buffer[received] = 0;

    char method[8], path[256];
    sscanf(buffer, "%7s %255s", method, path);

    // Remove leading /
    char *req_path = path + 1;
    if (strlen(req_path) == 0) req_path = "index.html";

    if (strcmp(method, "GET") == 0 && strcmp(path, "/api/students") == 0) {
        char *json = get_all_students_json();
        send_response(client, "200 OK", "application/json", json);
        free(json);
    }
    else if (strcmp(method, "POST") == 0 && strcmp(path, "/api/addStudent") == 0) {
        char *body = strstr(buffer, "\r\n\r\n");
        if (!body) {
            send_400(client);
        }
        else {
            body += 4;
            struct Student s;
            if (parse_post_data(body, &s) && add_student(&s)) {
                send_response(client, "200 OK", "application/json", "{\"status\":\"success\"}");
            }
            else {
                send_400(client);
            }
        }
    }
    else if (strcmp(method, "GET") == 0) {
        serve_static(client, req_path);
    }
    else if (strcmp(method, "OPTIONS") == 0) {
        send_response(client, "200 OK", "text/plain", "");
    }
    else {
        send_404(client);
    }

    closesocket(client);
}

int main() {
    WSADATA wsa;
    SOCKET listen_socket, client_socket;
    struct addrinfo hints, *res;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    printf("Starting server...\n");

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        printf("WSAStartup failed\n");
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        printf("getaddrinfo failed\n");
        WSACleanup();
        return 1;
    }

    listen_socket = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listen_socket == INVALID_SOCKET) {
        printf("Socket creation failed\n");
        freeaddrinfo(res);
        WSACleanup();
        return 1;
    }

    if (bind(listen_socket, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
        printf("Bind failed\n");
        freeaddrinfo(res);
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(res);

    if (listen(listen_socket, BACKLOG) == SOCKET_ERROR) {
        printf("Listen failed\n");
        closesocket(listen_socket);
        WSACleanup();
        return 1;
    }

    printf("Server listening on port %s...\n", PORT);

    while (1) {
        client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_socket == INVALID_SOCKET) {
            printf("Accept failed\n");
            break;
        }

        // Simple single-threaded handle client
        handle_client(client_socket);
    }

    closesocket(listen_socket);
    WSACleanup();
    return 0;
}
