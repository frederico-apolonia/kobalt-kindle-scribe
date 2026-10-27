#include "fbink.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int transfer_all(int file_fd, unsigned char *framebuffer, size_t length, bool restore)
{
    size_t offset = 0;
    while (offset < length) {
        ssize_t count = restore ? read(file_fd, framebuffer + offset, length - offset)
                                : write(file_fd, framebuffer + offset, length - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            fprintf(stderr, "fbguard: %s stopped at %zu/%zu: %s\n",
                    restore ? "restore" : "snapshot", offset, length,
                    count == 0 ? "unexpected EOF" : strerror(errno));
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 3 || (strcmp(argv[1], "snapshot") != 0 && strcmp(argv[1], "restore") != 0)) {
        fprintf(stderr, "usage: %s snapshot|restore FILE\n", argv[0]);
        return 2;
    }
    const bool restore = strcmp(argv[1], "restore") == 0;
    int framebuffer_fd = fbink_open();
    if (framebuffer_fd < 0) {
        fprintf(stderr, "fbguard: fbink_open failed: %d\n", framebuffer_fd);
        return 3;
    }
    FBInkConfig config = {0};
    config.is_quiet = true;
    if (fbink_init(framebuffer_fd, &config) < 0) {
        fprintf(stderr, "fbguard: fbink_init failed\n");
        fbink_close(framebuffer_fd);
        return 4;
    }
    struct fb_fix_screeninfo fixed = {0};
    if (ioctl(framebuffer_fd, FBIOGET_FSCREENINFO, &fixed) < 0 || fixed.smem_len == 0) {
        perror("fbguard: FBIOGET_FSCREENINFO");
        fbink_close(framebuffer_fd);
        return 5;
    }
    unsigned char *mapping = mmap(NULL, fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED,
                                  framebuffer_fd, 0);
    if (mapping == MAP_FAILED) {
        perror("fbguard: mmap");
        fbink_close(framebuffer_fd);
        return 6;
    }
    int flags = restore ? O_RDONLY : (O_WRONLY | O_CREAT | O_TRUNC);
    int file_fd = open(argv[2], flags, 0600);
    if (file_fd < 0) {
        perror("fbguard: open snapshot");
        munmap(mapping, fixed.smem_len);
        fbink_close(framebuffer_fd);
        return 7;
    }
    const int result = transfer_all(file_fd, mapping, fixed.smem_len, restore);
    if (restore && result == 0) {
        config.is_flashing = true;
        int marker = fbink_refresh(framebuffer_fd, 0, 0, 0, 0, &config);
        if (marker >= 0)
            (void)fbink_wait_for_complete(framebuffer_fd, marker);
        else
            fprintf(stderr, "fbguard: refresh failed: %d\n", marker);
    }
    (void)fsync(file_fd);
    close(file_fd);
    munmap(mapping, fixed.smem_len);
    fbink_close(framebuffer_fd);
    fprintf(stderr, "fbguard: %s %u bytes %s\n", argv[1], fixed.smem_len,
            result == 0 ? "ok" : "failed");
    return result == 0 ? 0 : 8;
}
