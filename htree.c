#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

static char *g_buffer = NULL;
static uint64_t g_file_size = 0;
static uint64_t g_chunk_size = 0;
static int g_num_threads = 0;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s filename num_threads\n", prog);
    exit(EXIT_FAILURE);
}

static uint32_t jenkins_one_at_a_time_hash(const uint8_t *key, uint64_t length) {
    uint64_t i = 0;
    uint32_t hash = 0;

    while (i != length) {
        hash += key[i++];
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

static void *worker(void *arg) {
    int idx = (int)(intptr_t)arg;
    int left = 2 * idx + 1;
    int right = 2 * idx + 2;

    uint64_t start = (uint64_t)idx * g_chunk_size;
    uint64_t len = g_chunk_size;
    if (idx == g_num_threads - 1) {
        len += g_file_size % g_num_threads;
    }

    uint32_t self_hash = jenkins_one_at_a_time_hash((uint8_t *)(g_buffer + start), len);

    pthread_t t_left, t_right;
    int has_left = 0, has_right = 0;

    if (left < g_num_threads) {
        if (pthread_create(&t_left, NULL, worker, (void *)(intptr_t)left) != 0) {
            perror("pthread_create left failed");
            pthread_exit(NULL);
        }
        has_left = 1;
    }
    if (right < g_num_threads) {
        if (pthread_create(&t_right, NULL, worker, (void *)(intptr_t)right) != 0) {
            perror("pthread_create right failed");
            pthread_exit(NULL);
        }
        has_right = 1;
    }

    uint32_t left_hash = 0, right_hash = 0;
    if (has_left) {
        void *ret_left = NULL;
        pthread_join(t_left, &ret_left);
        left_hash = *((uint32_t *)ret_left);
        free(ret_left);
    }
    if (has_right) {
        void *ret_right = NULL;
        pthread_join(t_right, &ret_right);
        right_hash = *((uint32_t *)ret_right);
        free(ret_right);
    }

    uint32_t final_hash = self_hash;
    char combined[64];
    if (has_left && has_right) {
        snprintf(combined, sizeof(combined), "%u%u%u", self_hash, left_hash, right_hash);
        final_hash = jenkins_one_at_a_time_hash((const uint8_t *)combined, strlen(combined));
    } else if (has_left) {
        snprintf(combined, sizeof(combined), "%u%u", self_hash, left_hash);
        final_hash = jenkins_one_at_a_time_hash((const uint8_t *)combined, strlen(combined));
    }

    uint32_t *ret = (uint32_t *)malloc(sizeof(uint32_t));
    if (ret == NULL) {
        perror("malloc failed");
        pthread_exit(NULL);
    }
    *ret = final_hash;
    pthread_exit(ret);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        usage(argv[0]);
    }

    g_num_threads = atoi(argv[2]);
    if (g_num_threads <= 0) {
        fprintf(stderr, "num_threads must be > 0\n");
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        return EXIT_FAILURE;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        perror("fstat failed");
        close(fd);
        return EXIT_FAILURE;
    }

    g_file_size = (uint64_t)st.st_size;
    if (g_file_size == 0) {
        printf("hash value = 0\n");
        close(fd);
        return EXIT_SUCCESS;
    }

    g_buffer = (char *)malloc(g_file_size);
    if (g_buffer == NULL) {
        perror("malloc failed");
        close(fd);
        return EXIT_FAILURE;
    }

    uint64_t nread = 0;
    while (nread < g_file_size) {
        ssize_t n = read(fd, g_buffer + nread, g_file_size - nread);
        if (n < 0) {
            perror("read failed");
            free(g_buffer);
            close(fd);
            return EXIT_FAILURE;
        }
        if (n == 0) {
            break;
        }
        nread += (uint64_t)n;
    }
    close(fd);

    if (nread != g_file_size) {
        fprintf(stderr, "read incomplete: got %" PRIu64 " of %" PRIu64 " bytes\n", nread, g_file_size);
        free(g_buffer);
        return EXIT_FAILURE;
    }

    if ((uint64_t)g_num_threads > g_file_size) {
        fprintf(stderr, "num_threads cannot exceed file size in bytes for this implementation\n");
        free(g_buffer);
        return EXIT_FAILURE;
    }

    g_chunk_size = g_file_size / (uint64_t)g_num_threads;
    if (g_chunk_size == 0) {
        fprintf(stderr, "chunk size became 0\n");
        free(g_buffer);
        return EXIT_FAILURE;
    }

    clock_t start = clock();
    pthread_t root;
    if (pthread_create(&root, NULL, worker, (void *)(intptr_t)0) != 0) {
        perror("pthread_create root failed");
        free(g_buffer);
        return EXIT_FAILURE;
    }

    void *ret = NULL;
    pthread_join(root, &ret);
    clock_t end = clock();
    if (ret == NULL) {
        fprintf(stderr, "root thread failed\n");
        free(g_buffer);
        return EXIT_FAILURE;
    }

    uint32_t final_hash = *((uint32_t *)ret);
    free(ret);
    free(g_buffer);

    printf("hash value = %u\n", final_hash);
    printf("time taken = %f \n", (end - start) * 1.0 / CLOCKS_PER_SEC);
    return EXIT_SUCCESS;
}
