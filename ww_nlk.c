/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <sys/types.h>
#include "nlk.h"

#define print_usage()								\
do {										\
	fprintf(stderr, "Usage: %s [-b] [-c char] [-i int] [-p pid] [-g groups]\n",	\
		argv[0]);							\
	fprintf(stderr, "-b: explicitly bind()\n");						\
	fprintf(stderr, "-c char: one char as data to transfer\n");		\
	fprintf(stderr, "-i int: one char as data to transfer\n");		\
	fprintf(stderr, "-p pid: target\n");					\
	fprintf(stderr, "-g groups: [1-3]\n");					\
} while (0)

int main(int argc, char *argv[])
{
	int opt;
	struct dumb db = { '\0', 0 };
	char *buf = NULL;
	ssize_t sz = 0;
	unsigned char bind_me = 0;

	int nlsk = 0;
	struct nlmsghdr *nlh = NULL;
	struct sockaddr_nl raddr = {
		.nl_family = AF_NETLINK,
		.nl_pid = 0, // to kernel(or multicast if set .nl_groups) 

		// multicast is viable only if groups has be created(by kernel)
		// because userspace seems not having sufficient privilege.
		// rmmod fails while any one still leeches on the groups.
		.nl_groups = 0
	};

	while ((opt = getopt(argc, argv, "bc:i:p:g:")) != -1) {
		switch (opt) {
		case 'b':
			bind_me = 1;
		case 'c':
			db.cc = optarg[0];
			break;
		case 'i':
			db.ii = atoi(optarg);
			break;
		case 'p':
			// overwrite target
			raddr.nl_pid = atoi(optarg);
			break;
		case 'g':
			// can be
			// 1 == NLK_GMASK_R
			// 2 == NLK_GMASK_W
			// 3 == NLK_GROUP, means 1 + 2
			raddr.nl_groups = atoi(optarg);
			if (raddr.nl_groups > NLK_GROUP) {
				print_usage();
				return -1;
			}
			break;
		default:
			print_usage();
			return 0;
		}
	}
	fprintf(stdout, "pid: %u\n", getpid());

	if (NULL == (buf = malloc(NLMSG_SPACE(sizeof(db))))) {
		perror(LINE_STR);
		return -1;
	}
	memset(buf, 0, NLMSG_SPACE(sizeof(db)));

	nlh = (struct nlmsghdr *)buf;
	nlh->nlmsg_flags = NLM_F_REQUEST;
	//nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_type = 0;		// opaque to netlink core
	nlh->nlmsg_seq = 1;		// opaque to netlink core
	nlh->nlmsg_pid = getpid();	// opaque to netlink core
	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(db));
	memcpy(NLMSG_DATA(nlh), &db, sizeof(db));


	if (-1 == (nlsk = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_TEST))) {
		perror(LINE_STR);
		free(buf);
		return -1;
	}

	// explicitly bind(). necessarily if you want recvfrom can resolve peer name.
	// after recvfrom(..., &peer, &peerlen), you'll get:
	// peer.nl_pid == laddr.nl_pid
	if (bind_me) {
		struct sockaddr_nl laddr = {
			.nl_family = AF_NETLINK,
			.nl_pid = getpid(),
			//.nl_pid = 0, // must to make sure unique per socket
				// (NOT per process!). another trick is to set
				// to 0 and then bind(). let kernel pick one.

			// no reading. .nl_groups is trivial for us
			.nl_groups = 0 // whatever
		};
		if (0 != bind(nlsk, (struct sockaddr *)&laddr, sizeof(laddr))) {
			perror(LINE_STR);
			return -1;
		}
	}

	if (0 > (sz = sendto(nlsk, buf, nlh->nlmsg_len, 0,
		(struct sockaddr *)&raddr, sizeof(raddr)))) {
		perror(LINE_STR);
		close(nlsk);
		free(buf);
		return -1;
	}
	fprintf(stdout, "[INFO] sent %d/%d\n", sz, nlh->nlmsg_len);
	close(nlsk);
	free(buf);
	return 0;
}

