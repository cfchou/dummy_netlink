/*
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/types.h>
#include "nlk.h"

#define print_usage()					\
do {							\
	fprintf(stderr, "Usage: %s [-g groups]\n",	\
		argv[0]);				\
} while (0)
//
volatile sig_atomic_t stop = 0;
static unsigned int recvin_groups = 0;

void signal_handler(int signum);
void data_input(char *buff, size_t sz);
int bind_mode_recv(int nlsk);

int main(int argc, char *argv[])
{
	int opt;
	int nlsk = 0;
	int ret = 0;
	fprintf(stdout, "pid: %u\n", getpid());

	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);
	signal(SIGHUP, signal_handler);

	while ((opt = getopt(argc, argv, "g:")) != -1) {
		switch (opt) {
		case 'g':
			// can be
			// 1 == NLK_GMASK_R
			// 2 == NLK_GMASK_W
			// 3 == NLK_GROUP, means 1 + 2
			recvin_groups = atoi(optarg);
			if (recvin_groups > NLK_GROUP) {
				print_usage();
				return -1;
			}
			break;
		default:
			print_usage();
			return 0;
		}
	}

	if (-1 == (nlsk = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_TEST))) {
		perror(LINE_STR);
		return -1;
	}

	ret = bind_mode_recv(nlsk);

	close(nlsk);
	fprintf(stderr, "INFO<%d> exit\n", __LINE__);
	return ret;
}

// |<1>hdr|<2>hdr padding|<3>payload|<4>payload padding|
/*
#define NLMSG_ALIGNTO   4
#define NLMSG_ALIGN(len) ( ((len)+NLMSG_ALIGNTO-1) & ~(NLMSG_ALIGNTO-1) )
#define NLMSG_HDRLEN     ((int) NLMSG_ALIGN(sizeof(struct nlmsghdr)))		// <1><2>

#define NLMSG_LENGTH(len) ((len)+NLMSG_ALIGN(NLMSG_HDRLEN))			//? nlmsghdr::nlmsg_len = <1><2><3>
#define NLMSG_SPACE(len) NLMSG_ALIGN(NLMSG_LENGTH(len))				//? <1><2><3><4>

#define NLMSG_DATA(nlh)  ((void*)(((char*)nlh) + NLMSG_LENGTH(0)))		// 
#define NLMSG_NEXT(nlh,len)      ((len) -= NLMSG_ALIGN((nlh)->nlmsg_len), \
                                  (struct nlmsghdr*)(((char*)(nlh)) + NLMSG_ALIGN((nlh)->nlmsg_len)))
#define NLMSG_OK(nlh,len) ((len) >= (int)sizeof(struct nlmsghdr) && \
                           (nlh)->nlmsg_len >= sizeof(struct nlmsghdr) && \
                           (nlh)->nlmsg_len <= (len))
#define NLMSG_PAYLOAD(nlh,len) ((nlh)->nlmsg_len - NLMSG_SPACE((len)))		//? NLMSG_PAYLOAD(nlh, 0)?
 */
void data_input(char *buff, size_t sz)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)buff;
	struct nlmsgerr *perr = NULL;
	struct dumb *pdb = NULL;
	
	for(; NLMSG_OK(nlh, sz); nlh = NLMSG_NEXT(nlh, sz)) {
		if (NLMSG_ERROR == nlh->nlmsg_type) {
			perr = (struct nlmsgerr *)NLMSG_DATA(nlh);
			if (0 == perr->error) {
				// this is an ACK
				fprintf(stdout, "[INFO] ACKed\n");
				continue;
			}
			fprintf(stderr, "ERR<%d>: NLMSG_ERROR, error: %d\n",
				__LINE__, perr->error);
			break;
		}
		if (NLM_F_ACK == nlh->nlmsg_flags) {
			fprintf(stdout, "[INFO] NLM_F_ACK\n");
			// FIXME
			// we should ack?
		}

		fprintf(stdout, "[INFO] nlmsg_len:%u, nlmsg_type:%u, "
			"nlmsg_flags:%d, nlmsg_seq:%d, nlmsg_pid:%d\n",
			nlh->nlmsg_len, nlh->nlmsg_type, nlh->nlmsg_flags,
			nlh->nlmsg_seq, nlh->nlmsg_pid);

		pdb = (struct dumb *)NLMSG_DATA(nlh);
		fprintf(stdout, "[INFO] recv one dumb %c %d\n", pdb->cc,
			pdb->ii);

		if (NLMSG_DONE == nlh->nlmsg_type) { // last one
			break;
		}
	}
	return;
}

int bind_mode_recv(int nlsk)
{
	char buff[NLMSG_GOODSIZE];
	ssize_t sz = 0;

	struct sockaddr raddr;
	socklen_t rlen = 0;

	fd_set rset;

	struct sockaddr_nl laddr = {
		.nl_family = AF_NETLINK,
		.nl_pid = getpid(),
		//.nl_pid = 0, // must to make sure unique per socket
			// (NOT per process!). another trick is to set
			// to 0 and then bind(). let kernel pick one.
		// multicast is viable only if groups has be created(by kernel)
		// because userspace seems not having sufficient privilege
		// hence rmmod fails while any one still leeches on the groups
		.nl_groups = recvin_groups
	};

	// to select have to bind first.
	if (0 != bind(nlsk, (struct sockaddr *)&laddr, sizeof(laddr))) {
		perror(LINE_STR);
		return -1;
	}

	FD_ZERO(&rset);
	FD_SET(nlsk, &rset);

	while (!stop) {
		if (-1 == select(nlsk + 1, &rset, NULL, NULL, NULL)) {
			perror(LINE_STR);
			if (EINTR == errno)
				continue;
		} else if (FD_ISSET(nlsk, &rset)) {
			memset(&raddr, 0, sizeof(raddr));
			memset(buff, 0, sizeof(buff));
			if (-1 == (sz = recvfrom(nlsk, buff, sizeof(buff),
				MSG_DONTWAIT, &raddr, &rlen))) {
				perror(LINE_STR);
			} else {
				if (rlen != sizeof(struct sockaddr_nl)) {
					fprintf(stderr,
						"ERR<%d>: %d not PF_NETLINK\n",
						__LINE__, raddr.sa_family);
					continue;
				}
				// unless sender set nl_pid/nl_group explicitly and bind()
				// otherwise we won't get meaningful peer name
				fprintf(stderr, "INFO<%d>: nl_pid %d, nl_group %d\n",
					__LINE__, ((struct sockaddr_nl *)&raddr)->nl_pid,
					((struct sockaddr_nl *)&raddr)->nl_groups);

				data_input(buff, sz);
			}

		} else {
			fprintf(stderr, "ERR<%d>\n", __LINE__);
		}
	}
	return 0;
}

void signal_handler(int signum)
{
	stop = 1;
}

// unlike inet protocols, netlink must bind. otherwise netlink core won't know
// where to send
int loop_mode_recv(int nlsk)
{
	char buff[NLMSG_GOODSIZE];
	ssize_t sz = 0;

	struct sockaddr raddr;
	socklen_t rlen = 0;
	while (!stop) {
		memset(&raddr, 0, sizeof(raddr));
		memset(buff, 0, sizeof(buff));
		if (-1 == (sz = recvfrom(nlsk, buff, sizeof(buff), 0, &raddr,
			&rlen))) {
			perror(LINE_STR);
			if (EINTR == errno)
				continue;
			break;
		}
		if (rlen != sizeof(struct sockaddr_nl)) {
			fprintf(stderr,
				"ERR<%d>: %d not PF_NETLINK\n",
				__LINE__, raddr.sa_family);
			continue;
		}
		fprintf(stderr, "INFO<%d>: nl_pid %d, nl_group %d\n",
			__LINE__, ((struct sockaddr_nl *)&raddr)->nl_pid,
			((struct sockaddr_nl *)&raddr)->nl_groups);
		data_input(buff, sz);
	}
	return 0;
}
