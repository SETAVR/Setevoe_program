#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* Reserve standard descriptors so socket() cannot return 0, 1 or 2. */
static int prepare_standard_fds(void) {
    for (int i = 0; i < 3; ++i) {
        if (fcntl(i, F_GETFD) >= 0) continue;
        if (errno != EBADF) { perror("fcntl standard fd"); return -1; }
        int fd = open("/dev/null", O_RDWR);
        if (fd < 0) { perror("open /dev/null"); return -1; }
        if (fd != i) {
            int copied = dup2(fd, i);
            int saved = errno;
            if (close(fd) < 0) { perror("close temporary fd"); return -1; }
            if (copied < 0) { errno = saved; perror("dup2"); return -1; }
        }
    }
    return 0;
}

/* Do not show the descriptor temporarily used to read /proc/self/fd. */
static int print_fds(const char *heading) {
    printf("%s\n", heading);
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) { perror("opendir /proc/self/fd"); return -1; }
    int own_fd = dirfd(dir), result = 0;
    if (own_fd < 0) { perror("dirfd"); result = -1; }
    while (result == 0) {
        errno = 0;
        struct dirent *entry = readdir(dir);
        if (!entry) {
            if (errno) { perror("readdir"); result = -1; }
            break;
        }
        if (entry->d_name[0] == '.') continue;
        char *end;
        long fd = strtol(entry->d_name, &end, 10);
        if (*end || fd == own_fd) continue;
        char path[512], target[4096];
        int size = snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);
        if (size < 0 || (size_t)size >= sizeof(path)) {
            fprintf(stderr, "FD path too long\n"); result = -1; break;
        }
        ssize_t n = readlink(path, target, sizeof(target) - 1);
        if (n < 0) { perror("readlink"); result = -1; break; }
        target[n] = '\0';
        printf("  fd=%ld -> %s\n", fd, target);
    }
    if (closedir(dir) < 0) { perror("closedir"); result = -1; }
    return result;
}

static int parse_port(const char *text, unsigned short *port) {
    if (!*text) return -1;
    for (const char *p = text; *p; ++p)
        if (*p < '0' || *p > '9') return -1;
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || *end || value > 65535) return -1;
    *port = (unsigned short)value;
    return 0;
}

static int print_address(const char *label, const struct sockaddr_in *addr) {
    char ip[INET_ADDRSTRLEN];
    if (!inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip))) {
        perror("inet_ntop"); return -1;
    }
    printf("%s%s:%u\n", label, ip, (unsigned)ntohs(addr->sin_port));
    return 0;
}

static void usage(const char *program) {
    printf("Usage: %s <domain> <type> <ip> <port> [--bind]\n", program);
    puts("domain: AF_INET; type: SOCK_DGRAM or SOCK_STREAM");
    puts("No arguments: variant 6 basic (192.168.1.1:67, UDP)");
    puts("--medium: variant 6 bind (0.0.0.0:5000, UDP)");
    puts("--help: show this help");
}

int main(int argc, char **argv) {
    const char *ip = "192.168.1.1", *port_text = "67";
    int type = SOCK_DGRAM, do_bind = 0;
    if (prepare_standard_fds() < 0) return EXIT_FAILURE;
    /* Keep stdout and stderr in chronological order in redirected logs. */
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]); return EXIT_SUCCESS;
    } else if (argc == 2 && strcmp(argv[1], "--medium") == 0) {
        ip = "0.0.0.0"; port_text = "5000"; do_bind = 1;
    } else if (argc != 1) {
        if (argc != 5 && argc != 6) { usage(argv[0]); return EXIT_FAILURE; }
        if (strcmp(argv[1], "AF_INET") != 0) {
            fprintf(stderr, "Invalid domain: %s (expected AF_INET)\n", argv[1]);
            return EXIT_FAILURE;
        }
        if (strcmp(argv[2], "SOCK_DGRAM") == 0) type = SOCK_DGRAM;
        else if (strcmp(argv[2], "SOCK_STREAM") == 0) type = SOCK_STREAM;
        else {
            fprintf(stderr, "Invalid socket type: %s\n", argv[2]);
            return EXIT_FAILURE;
        }
        ip = argv[3]; port_text = argv[4];
        if (argc == 6) {
            if (strcmp(argv[5], "--bind") != 0) {
                fprintf(stderr, "Unknown option: %s\n", argv[5]);
                return EXIT_FAILURE;
            }
            do_bind = 1;
        }
    }
    unsigned short port;
    if (parse_port(port_text, &port) < 0) {
        fprintf(stderr, "Invalid port: %s (expected 0..65535)\n", port_text);
        return EXIT_FAILURE;
    }
    /* Zero initialization also clears sin_zero. Address fields use network order. */
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    int converted = inet_pton(AF_INET, ip, &address.sin_addr);
    if (converted == 0) {
        fprintf(stderr, "Invalid IPv4 address: %s (inet_pton=0)\n", ip);
        return EXIT_FAILURE;
    }
    if (converted < 0) { perror("inet_pton"); return EXIT_FAILURE; }
    if (do_bind && strcmp(ip, "0.0.0.0") == 0)
        address.sin_addr.s_addr = htonl(INADDR_ANY);

    int fd = socket(AF_INET, type, 0);
    if (fd < 0) { perror("socket"); return EXIT_FAILURE; }
    int result = EXIT_FAILURE;
    printf("socket: domain=AF_INET type=%s protocol=0 fd=%d\n",
           type == SOCK_DGRAM ? "SOCK_DGRAM" : "SOCK_STREAM", fd);
    /* Variant 6: explicitly check the descriptor, including closed-stdio runs. */
    if (fd < 3) { fprintf(stderr, "FAIL: socket fd < 3\n"); goto cleanup; }
    puts("Variant 6 check: fd >= 3: PASS");
    if (print_address("Configured address: ", &address) < 0) goto cleanup;
    const unsigned char *bytes = (const unsigned char *)&address.sin_port;
    printf("Port: host=%u network bytes=%02X %02X restored=%u\n",
           (unsigned)port, bytes[0], bytes[1], (unsigned)ntohs(address.sin_port));
    if (do_bind) {
        if (bind(fd, (const struct sockaddr *)&address, sizeof(address)) < 0) {
            int saved = errno;
            perror("bind");
            if (saved == EADDRINUSE)
                fprintf(stderr, "EADDRINUSE: port is already occupied\n");
            goto cleanup;
        }
        struct sockaddr_in actual = {0};
        socklen_t length = sizeof(actual);
        if (getsockname(fd, (struct sockaddr *)&actual, &length) < 0) {
            perror("getsockname"); goto cleanup;
        }
        if (print_address("Bound address: ", &actual) < 0) goto cleanup;
    }
    if (print_fds("FD table before close:") < 0) goto cleanup;
    result = EXIT_SUCCESS;
cleanup:
    /* One cleanup path; do not retry close() after an error. */
    if (close(fd) < 0) { perror("close socket"); return EXIT_FAILURE; }
    printf("close(%d)=0\n", fd);
    errno = 0;
    if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) {
        fprintf(stderr, "FAIL: descriptor was not released\n");
        result = EXIT_FAILURE;
    } else puts("Released descriptor check: EBADF: PASS");
    if (print_fds("FD table after close:") < 0) result = EXIT_FAILURE;
    return result;
}
