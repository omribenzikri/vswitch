#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/if.h>
#include <linux/if_tun.h>

int main() {
    const int fd = open("/dev/net/tun", O_RDWR);
    if (fd == -1) {
        perror("vport");
        exit(EXIT_FAILURE);
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;

    snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s", "tun0");

    int ret = ioctl(fd, TUNSETIFF, &ifr);
    if (ret == -1) {
        perror("vport");
        close(fd);
        exit(EXIT_FAILURE);
    }

    printf("Sucessfully opened tap0 interface :)\n");
    close(fd);

    return EXIT_SUCCESS;
}