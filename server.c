#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
#define GET_SOCKET_ERROR WSAGetLastError()
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET close
#define GET_SOCKET_ERROR errno
#endif

#define PORT 8080
#define BUFFER_SIZE 8192
#define MAX_TOTAL_MESSAGES 500
#define MAX_MESSAGES_PER_USER 10
#define MAX_USERNAME_LENGTH 64
#define MAX_TEXT_LENGTH 384

typedef struct {
    unsigned long id;
    char username[MAX_USERNAME_LENGTH];
    char text[MAX_TEXT_LENGTH];
    char timestamp[32];
} ChatMessage;

static ChatMessage messages[MAX_TOTAL_MESSAGES];
static int message_count = 0;
static unsigned long next_message_id = 1;
static volatile sig_atomic_t keep_running = 1;

static int initialize_networking(void) {
#ifdef _WIN32
    WSADATA wsa_data;
    return WSAStartup(MAKEWORD(2, 2), &wsa_data);
#else
    return 0;
#endif
}

static void shutdown_networking(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

static void handle_signal(int sig) {
    (void)sig;
    keep_running = 0;
}

static void send_response(socket_t client_fd, const char *status, const char *content_type, const char *body) {
    char header[512];
    size_t body_len = strlen(body);
    int header_len = snprintf(
        header,
        sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "\r\n",
        status,
        content_type,
        body_len
    );

    send(client_fd, header, (size_t)header_len, 0);
    send(client_fd, body, body_len, 0);
}

static bool load_file(const char *path, char *out, size_t out_size) {
    FILE *file = fopen(path, "rb");
    size_t bytes_read;

    if (!file) {
        return false;
    }

    bytes_read = fread(out, 1, out_size - 1, file);
    out[bytes_read] = '\0';
    fclose(file);
    return true;
}

static void trim_whitespace(char *text) {
    size_t start = 0;
    size_t end = strlen(text);

    while (text[start] && isspace((unsigned char)text[start])) {
        start++;
    }

    while (end > start && isspace((unsigned char)text[end - 1])) {
        end--;
    }

    if (start > 0) {
        memmove(text, text + start, end - start);
    }

    text[end - start] = '\0';
}

static void escape_json_string(const char *src, char *dest, size_t dest_size) {
    size_t i = 0;
    size_t j = 0;

    while (src[i] && j + 2 < dest_size) {
        unsigned char ch = (unsigned char)src[i];

        if (ch == '"' || ch == '\\') {
            dest[j++] = '\\';
            dest[j++] = (char)ch;
        } else if (ch == '\n' || ch == '\r') {
            dest[j++] = '\\';
            dest[j++] = 'n';
        } else if (ch >= 32) {
            dest[j++] = (char)ch;
        }

        i++;
    }

    dest[j] = '\0';
}

static void decode_component(const char *src, char *dest, size_t dest_size) {
    size_t i = 0;
    size_t j = 0;

    while (src[i] && j + 1 < dest_size) {
        if (src[i] == '+') {
            dest[j++] = ' ';
            i++;
            continue;
        }

        if (
            src[i] == '%' &&
            isxdigit((unsigned char)src[i + 1]) &&
            isxdigit((unsigned char)src[i + 2])
        ) {
            char hex[3];
            hex[0] = src[i + 1];
            hex[1] = src[i + 2];
            hex[2] = '\0';
            dest[j++] = (char)strtol(hex, NULL, 16);
            i += 3;
            continue;
        }

        dest[j++] = src[i++];
    }

    dest[j] = '\0';
}

static void parse_form_field(const char *body, const char *key, char *out, size_t out_size) {
    char pattern[64];
    const char *found;
    size_t value_len = 0;
    char encoded[512];

    snprintf(pattern, sizeof(pattern), "%s=", key);
    found = strstr(body, pattern);
    out[0] = '\0';

    if (!found) {
        return;
    }

    found += strlen(pattern);
    while (found[value_len] && found[value_len] != '&' && value_len + 1 < sizeof(encoded)) {
        encoded[value_len] = found[value_len];
        value_len++;
    }

    encoded[value_len] = '\0';
    decode_component(encoded, out, out_size);
}

static void get_query_value(const char *query, const char *key, char *out, size_t out_size) {
    char pattern[64];
    const char *found;
    size_t value_len = 0;
    char encoded[256];

    out[0] = '\0';
    if (!query || query[0] == '\0') {
        return;
    }

    snprintf(pattern, sizeof(pattern), "%s=", key);
    found = strstr(query, pattern);
    if (!found) {
        return;
    }

    found += strlen(pattern);
    while (found[value_len] && found[value_len] != '&' && value_len + 1 < sizeof(encoded)) {
        encoded[value_len] = found[value_len];
        value_len++;
    }

    encoded[value_len] = '\0';
    decode_component(encoded, out, out_size);
}

static int get_content_length(const char *request) {
    const char *header = strstr(request, "Content-Length:");

    if (!header) {
        return 0;
    }

    return atoi(header + strlen("Content-Length:"));
}

static void split_path_and_query(char *raw_path, char **path_only, char **query) {
    char *question = strchr(raw_path, '?');

    *path_only = raw_path;
    *query = NULL;

    if (question) {
        *question = '\0';
        *query = question + 1;
    }
}

static void remove_message_at(int index) {
    for (int i = index + 1; i < message_count; i++) {
        messages[i - 1] = messages[i];
    }

    if (message_count > 0) {
        message_count--;
    }
}

static int count_user_messages(const char *username) {
    int count = 0;

    for (int i = 0; i < message_count; i++) {
        if (strcmp(messages[i].username, username) == 0) {
            count++;
        }
    }

    return count;
}

static void enforce_user_limit(const char *username) {
    while (count_user_messages(username) > MAX_MESSAGES_PER_USER) {
        for (int i = 0; i < message_count; i++) {
            if (strcmp(messages[i].username, username) == 0) {
                remove_message_at(i);
                break;
            }
        }
    }
}

static void store_message(const char *username, const char *text) {
    time_t now;
    struct tm *local_time;

    if (message_count == MAX_TOTAL_MESSAGES) {
        remove_message_at(0);
    }

    now = time(NULL);
    local_time = localtime(&now);

    messages[message_count].id = next_message_id++;
    snprintf(messages[message_count].username, sizeof(messages[message_count].username), "%s", username);
    snprintf(messages[message_count].text, sizeof(messages[message_count].text), "%s", text);
    if (local_time) {
        strftime(
            messages[message_count].timestamp,
            sizeof(messages[message_count].timestamp),
            "%Y-%m-%d %H:%M:%S",
            local_time
        );
    } else {
        snprintf(messages[message_count].timestamp, sizeof(messages[message_count].timestamp), "unknown");
    }
    message_count++;

    enforce_user_limit(username);
}

static void append_message_json(char *json, size_t json_size, size_t *used, const ChatMessage *message, bool first) {
    char escaped_user[MAX_USERNAME_LENGTH * 2];
    char escaped_text[MAX_TEXT_LENGTH * 2];
    char escaped_timestamp[64];

    escape_json_string(message->username, escaped_user, sizeof(escaped_user));
    escape_json_string(message->text, escaped_text, sizeof(escaped_text));
    escape_json_string(message->timestamp, escaped_timestamp, sizeof(escaped_timestamp));

    *used += snprintf(
        json + *used,
        json_size - *used,
        "%s{\"id\":%lu,\"username\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}",
        first ? "" : ",",
        message->id,
        escaped_user,
        escaped_text,
        escaped_timestamp
    );
}

static void build_messages_json(char *json, size_t json_size, const char *filter_user, int limit) {
    size_t used = 0;
    int matched_indices[MAX_TOTAL_MESSAGES];
    int matched_count = 0;
    int start_index = 0;
    bool first = true;

    used += snprintf(json + used, json_size - used, "[");

    for (int i = 0; i < message_count; i++) {
        if (filter_user[0] != '\0' && strcmp(messages[i].username, filter_user) != 0) {
            continue;
        }
        matched_indices[matched_count++] = i;
    }

    if (limit > 0 && matched_count > limit) {
        start_index = matched_count - limit;
    }

    for (int i = start_index; i < matched_count && used < json_size; i++) {
        append_message_json(json, json_size, &used, &messages[matched_indices[i]], first);
        first = false;
    }

    if (used < json_size) {
        snprintf(json + used, json_size - used, "]");
    } else {
        json[json_size - 1] = '\0';
    }
}

static void build_users_json(char *json, size_t json_size) {
    char seen[MAX_TOTAL_MESSAGES][MAX_USERNAME_LENGTH];
    int seen_count = 0;
    size_t used = 0;
    bool first = true;

    used += snprintf(json + used, json_size - used, "[");

    for (int i = 0; i < message_count; i++) {
        bool already_seen = false;
        int user_message_count = 0;
        char escaped_user[MAX_USERNAME_LENGTH * 2];

        for (int j = 0; j < seen_count; j++) {
            if (strcmp(seen[j], messages[i].username) == 0) {
                already_seen = true;
                break;
            }
        }

        if (already_seen) {
            continue;
        }

        snprintf(seen[seen_count], sizeof(seen[seen_count]), "%s", messages[i].username);
        seen_count++;
        user_message_count = count_user_messages(messages[i].username);
        escape_json_string(messages[i].username, escaped_user, sizeof(escaped_user));

        used += snprintf(
            json + used,
            json_size - used,
            "%s{\"username\":\"%s\",\"messageCount\":%d}",
            first ? "" : ",",
            escaped_user,
            user_message_count
        );
        first = false;
    }

    if (used < json_size) {
        snprintf(json + used, json_size - used, "]");
    } else {
        json[json_size - 1] = '\0';
    }
}

static void build_stats_json(char *json, size_t json_size) {
    snprintf(
        json,
        json_size,
        "{\"totalMessages\":%d,\"maxTotalMessages\":%d,\"maxMessagesPerUser\":%d}",
        message_count,
        MAX_TOTAL_MESSAGES,
        MAX_MESSAGES_PER_USER
    );
}

static void handle_send(socket_t client_fd, const char *body) {
    char username[MAX_USERNAME_LENGTH];
    char message[MAX_TEXT_LENGTH];
    char response[1024];
    char escaped_user[MAX_USERNAME_LENGTH * 2];
    char escaped_text[MAX_TEXT_LENGTH * 2];
    char escaped_timestamp[64];

    parse_form_field(body, "username", username, sizeof(username));
    parse_form_field(body, "message", message, sizeof(message));
    trim_whitespace(username);
    trim_whitespace(message);

    if (username[0] == '\0' || message[0] == '\0') {
        send_response(
            client_fd,
            "400 Bad Request",
            "application/json",
            "{\"ok\":false,\"error\":\"Username and message are required.\"}"
        );
        return;
    }

    store_message(username, message);
    escape_json_string(username, escaped_user, sizeof(escaped_user));
    escape_json_string(message, escaped_text, sizeof(escaped_text));
    escape_json_string(messages[message_count - 1].timestamp, escaped_timestamp, sizeof(escaped_timestamp));

    snprintf(
        response,
        sizeof(response),
        "{\"ok\":true,\"stored\":{\"id\":%lu,\"username\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}}",
        messages[message_count - 1].id,
        escaped_user,
        escaped_text,
        escaped_timestamp
    );

    send_response(client_fd, "200 OK", "application/json", response);
}

static void handle_get_messages(socket_t client_fd, const char *query) {
    char user_filter[MAX_USERNAME_LENGTH];
    char limit_text[32];
    char json[MAX_TOTAL_MESSAGES * 512];
    int limit = 0;

    get_query_value(query, "user", user_filter, sizeof(user_filter));
    get_query_value(query, "limit", limit_text, sizeof(limit_text));
    trim_whitespace(user_filter);

    if (limit_text[0] != '\0') {
        limit = atoi(limit_text);
        if (limit < 0) {
            limit = 0;
        }
    }

    build_messages_json(json, sizeof(json), user_filter, limit);
    send_response(client_fd, "200 OK", "application/json", json);
}

static void handle_get_users(socket_t client_fd) {
    char json[MAX_TOTAL_MESSAGES * 128];
    build_users_json(json, sizeof(json));
    send_response(client_fd, "200 OK", "application/json", json);
}

static void handle_get_stats(socket_t client_fd) {
    char json[256];
    build_stats_json(json, sizeof(json));
    send_response(client_fd, "200 OK", "application/json", json);
}

static void handle_client(socket_t client_fd) {
    char buffer[BUFFER_SIZE];
    char method[8];
    char raw_path[256];
    char *path;
    char *query;
    char *body;
    int received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0) {
        return;
    }

    buffer[received] = '\0';

    if (sscanf(buffer, "%7s %255s", method, raw_path) != 2) {
        send_response(client_fd, "400 Bad Request", "text/plain", "Invalid request");
        return;
    }

    split_path_and_query(raw_path, &path, &query);

    body = strstr(buffer, "\r\n\r\n");
    if (body) {
        body += 4;
    } else {
        body = buffer + received;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/") == 0) {
        char html[32768];

        if (!load_file("index.html", html, sizeof(html))) {
            send_response(client_fd, "500 Internal Server Error", "text/plain", "Unable to load index.html");
            return;
        }

        send_response(client_fd, "200 OK", "text/html; charset=utf-8", html);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/messages") == 0) {
        handle_get_messages(client_fd, query);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/users") == 0) {
        handle_get_users(client_fd);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/stats") == 0) {
        handle_get_stats(client_fd);
        return;
    }

    if (strcmp(method, "POST") == 0 && strcmp(path, "/send") == 0) {
        int content_length = get_content_length(buffer);
        int body_length = (int)(buffer + received - body);

        while (body_length < content_length && received < (int)sizeof(buffer) - 1) {
            int more = recv(client_fd, buffer + received, sizeof(buffer) - 1 - (size_t)received, 0);
            if (more <= 0) {
                break;
            }
            received += more;
            buffer[received] = '\0';
            body = strstr(buffer, "\r\n\r\n");
            body = body ? body + 4 : buffer + received;
            body_length = (int)(buffer + received - body);
        }

        handle_send(client_fd, body);
        return;
    }

    send_response(client_fd, "404 Not Found", "text/plain", "Not found");
}

int main(void) {
    socket_t server_fd;
    int opt = 1;
    struct sockaddr_in address;

    if (initialize_networking() != 0) {
        fprintf(stderr, "Failed to initialize networking.\n");
        return 1;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == (socket_t)-1) {
        perror("socket");
        shutdown_networking();
        return 1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        CLOSE_SOCKET(server_fd);
        shutdown_networking();
        return 1;
    }

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        CLOSE_SOCKET(server_fd);
        shutdown_networking();
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        CLOSE_SOCKET(server_fd);
        shutdown_networking();
        return 1;
    }

    printf("Chat server running on http://localhost:%d\n", PORT);
    printf("Server keeps the latest %d messages per user in memory.\n", MAX_MESSAGES_PER_USER);

    while (keep_running) {
        struct sockaddr_in client_address;
        socklen_t client_length = sizeof(client_address);
        socket_t client_fd = accept(server_fd, (struct sockaddr *)&client_address, &client_length);

        if (client_fd == (socket_t)-1) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "accept failed: %d\n", GET_SOCKET_ERROR);
            break;
        }

        handle_client(client_fd);
        CLOSE_SOCKET(client_fd);
    }

    CLOSE_SOCKET(server_fd);
    shutdown_networking();
    return 0;
}
