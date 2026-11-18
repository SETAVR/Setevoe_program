#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

// Показываем FD, не считая временный дескриптор самой директории.
int print_fds(const char *title) {
    puts(title);
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) { perror("opendir"); return -1; }
    int own = dirfd(dir), result = 0;
    if (own == -1) { perror("dirfd"); result = -1; }
    while (result == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) { perror("readdir"); result = -1; }
            break;
        }
        if (entry->d_name[0] != '.' && atoi(entry->d_name) != own)
            printf("FD: %s\n", entry->d_name);
    }
    if (closedir(dir) == -1) { perror("closedir"); result = -1; }
    return result;
}

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if ((argc != 5 && argc != 6) ||
        (argc == 6 && strcmp(argv[5], "--bind") != 0)) {
        fprintf(stderr, "Usage: %s <domain> <type> <ip> <port> [--bind]\n", argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "AF_INET") != 0) {
        fprintf(stderr, "Invalid domain: use AF_INET\n");
        return 1;
    }
    int type;
    if (strcmp(argv[2], "SOCK_STREAM") == 0) type = SOCK_STREAM;
    else if (strcmp(argv[2], "SOCK_DGRAM") == 0) type = SOCK_DGRAM;
    else { fprintf(stderr, "Invalid socket type\n"); return 1; }

    char *end;
    errno = 0;
    long port = strtol(argv[4], &end, 10);
    if (errno == ERANGE || end == argv[4] || *end || port < 0 || port > 65535) {
        fprintf(stderr, "Invalid port: expected 0..65535\n");
        return 1;
    }
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    int converted = inet_pton(AF_INET, argv[3], &addr.sin_addr);
    if (converted == 0) { fprintf(stderr, "Invalid IPv4 address\n"); return 1; }
    if (converted == -1) { perror("inet_pton"); return 1; }

    int fd = socket(AF_INET, type, 0);
    if (fd == -1) { perror("socket"); return 1; }
    int result = 1;
    printf("Socket FD: %d\n", fd);
    // Дополнительное требование варианта 6.
    if (fd < 3) { fprintf(stderr, "Error: socket FD < 3\n"); goto cleanup; }
    puts("FD >= 3: OK");
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip))) {
        perror("inet_ntop"); goto cleanup;
    }
    printf("Address: %s:%u\n", ip, (unsigned)ntohs(addr.sin_port));
    const unsigned char *bytes = (const unsigned char *)&addr.sin_port;
    printf("Port in network order: %02X %02X\n", bytes[0], bytes[1]);

    if (argc == 6) {
        if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("bind"); goto cleanup;
        }
        socklen_t size = sizeof(addr);
        if (getsockname(fd, (struct sockaddr *)&addr, &size) == -1) {
            perror("getsockname"); goto cleanup;
        }
        printf("Bound port: %u\n", (unsigned)ntohs(addr.sin_port));
    }
    if (print_fds("Before close:") == -1) goto cleanup;
    result = 0;
cleanup:
    if (close(fd) == -1) { perror("close"); return 1; }
    puts("Socket closed");
    if (print_fds("After close:") == -1) result = 1;
    return result;
}
