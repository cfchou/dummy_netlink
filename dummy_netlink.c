/*
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>

#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

#include <net/netlink.h>
#include <net/net_namespace.h>
#include "nlk.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chou Chifeng <cfchou@gmail.com>");
MODULE_DESCRIPTION("dummy proc/seq");
MODULE_ALIAS("dummy_proc");

static unsigned int r_grps __read_mostly = NLK_GROUPS;
module_param(r_grps, uint, 0600);
MODULE_PARM_DESC(r_grps, "recv in groups. default is 3(2 groups, 0011).");

static unsigned int b_grp __read_mostly = NLK_GROUP_2;
module_param(b_grp, uint, 0600);
MODULE_PARM_DESC(b_grp, "broadcast group. default is 2(0010).");
#define DEBUGP printk

//struct work_struct *writer_work = NULL;
static void nlk_periodic_send(struct work_struct *work);
DECLARE_DELAYED_WORK(writer_work, nlk_periodic_send);

static void nlsk_recv(struct sk_buff *skb);
static struct sock	*nlsk = NULL;
static unsigned int kseq = 0;

static int nlk_uni_send(struct sock *nlsk, u32 pid, struct dumb const *pdb,
	int nonblock);

// recv 2 groups
// if we're not going to block, we can proceed them all together
static void nlsk_recv(struct sk_buff *skb)
{
	struct nlmsghdr *nlh = NULL;
	struct nlmsgerr *perr = NULL;
	struct dumb *pdb = NULL;
	DEBUGP(KERN_ALERT "[INFO] nlsk_recv\n");
	for(nlh = nlmsg_hdr(skb); NLMSG_OK(nlh, skb->len);
		nlh = NLMSG_NEXT(nlh, skb->len)) {
		DEBUGP(KERN_ALERT "[INFO] nlmsg_pid, nlmsg_seq: %u, %u\n",
			nlh->nlmsg_pid, nlh->nlmsg_seq);
		if (NLMSG_ERROR == nlh->nlmsg_type) {
			perr = (struct nlmsgerr *)NLMSG_DATA(nlh);
			if (0 == perr->error) {
				// this is an ACK
				DEBUGP(KERN_ALERT "[INFO] ACKed\n");
				continue;
			}
			DEBUGP(KERN_ALERT "[ERR] NLMSG_ERROR, error: %d\n",
				perr->error);
			break;
		}

		if (NLM_F_ACK == nlh->nlmsg_flags) {
			DEBUGP(KERN_ALERT "[INFO] NLM_F_ACK\n");
			netlink_ack(skb, nlh, 0);
		}

		pdb = (struct dumb *)NLMSG_DATA(nlh);
		DEBUGP(KERN_ALERT "[INFO] recv one dumb %c %d\n", pdb->cc,
			pdb->ii);
		// unicast back only to user app
		if (0 != pdb->ii)
			nlk_uni_send(nlsk, pdb->ii, pdb, 1);
		
		if (NLMSG_DONE == nlh->nlmsg_type) // last one
			break;
	}
}

int nlk_uni_send(struct sock *nlsk, u32 pid, struct dumb const *pdb, int nonblock)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	if (NULL == (skb = alloc_skb(NLMSG_SPACE(sizeof(struct dumb)),
		GFP_ATOMIC))) {
		DEBUGP(KERN_ALERT "[ERR] alloc_skb\n");
		return -ENOMEM;
	}
	// __nlmsg_put()
	nlh = (struct nlmsghdr *)skb_put(skb, NLMSG_SPACE(sizeof(struct dumb)));
	nlh->nlmsg_type = 0;
	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct dumb));
	nlh->nlmsg_flags = NLM_F_REQUEST;
	nlh->nlmsg_pid = 0;		// opaque to netlink core
	nlh->nlmsg_seq = 0;		// opaque to netlink core
	memset(NLMSG_DATA(nlh) + sizeof(struct dumb), 0,
		NLMSG_SPACE(sizeof(struct dumb)) - NLMSG_LENGTH(sizeof(struct dumb)));
	memcpy(NLMSG_DATA(nlh), pdb, sizeof(struct dumb));

	// could it be optional?
	/*
	NETLINK_CB(skb).pid = 0;
	NETLINK_CB(skb).dst_group = r_grps;
	*/

	return netlink_unicast(nlsk, skb, pid, nonblock); // 1: non-block
}

// work_func_t
void nlk_periodic_send(struct work_struct *work)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh = NULL;
	struct dumb db = { 'A', 1000 };
	int ret = 0;

	DEBUGP(KERN_ALERT "[INFO] nlk_periodic_send\n");

	if (NULL == (skb = alloc_skb(NLMSG_SPACE(sizeof(struct dumb)),
		GFP_KERNEL))) {
		DEBUGP(KERN_ALERT "[ERR] alloc_skb\n");
		//netlink_kernel_release(sk);
		schedule_delayed_work(&writer_work, 15 * HZ); // to keventd
		return;
	}


#if 0
	nlh = NLMSG_PUT(skb, 0, kseq++, NLMSG_DONE,
		NLMSG_SPACE(sizeof(struct dumb)) - sizeof(*nlh));
	
	memcpy(NLMSG_DATA(nlh), &db, sizeof(struct dumb));
	NETLINK_CB(skb).dst_group = NLK_GROUP;
#endif
	DEBUGP(KERN_ALERT "[INFO] dumb:%d nlmsghdr:%d, NLMSG_SPACE:%d,"
		" NLMSG_LENGTH:%d\n", sizeof(struct dumb),
		sizeof(struct nlmsghdr), NLMSG_SPACE(sizeof(struct dumb)),
		NLMSG_LENGTH(sizeof(struct dumb)));
	DEBUGP(KERN_ALERT "[INFO] len:%d put:%d head:%p data:%p tail:%#lx"
		" end:%#lx, NLMSG_DATA:%p\n", skb->len,
		NLMSG_SPACE(sizeof(struct dumb)), skb->head, skb->data,
		(unsigned long)skb->tail, (unsigned long)skb->end, NLMSG_DATA(nlh));

	// __nlmsg_put()
	nlh = (struct nlmsghdr *)skb_put(skb, NLMSG_SPACE(sizeof(struct dumb)));
	//nlh->nlmsg_type = NLMSG_DONE;
	nlh->nlmsg_type = 0;
	nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct dumb));
	nlh->nlmsg_flags = NLM_F_REQUEST;
	//nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	nlh->nlmsg_pid = 0;		// opaque to netlink core
	nlh->nlmsg_seq = kseq++;	// opaque to netlink core
	memset(NLMSG_DATA(nlh) + sizeof(struct dumb), 0,
		NLMSG_SPACE(sizeof(struct dumb)) - NLMSG_LENGTH(sizeof(struct dumb)));

	memcpy(NLMSG_DATA(nlh), &db, sizeof(struct dumb));

	// could it be optional?
	/*
	NETLINK_CB(skb).pid = 0;
	NETLINK_CB(skb).dst_group = b_grp;
	*/

	DEBUGP(KERN_ALERT "[INFO] b_grp: %u\n", b_grp);

	if (0 == (ret = netlink_broadcast(nlsk, skb, 0, b_grp,
		GFP_KERNEL))) {
		schedule_delayed_work(&writer_work, 15 * HZ); // to keventd
		return;
	}
	DEBUGP(KERN_ALERT "[ERR] netlink_broadcast:%d\n", ret);
	schedule_delayed_work(&writer_work, 15 * HZ); // to keventd
}

#if 0
// recv 1 group, and wake up a thread to get job done
static void nlsk_recv_th(struct sk_buff *skb)
{
	
	/*
	size_t msg_len = 0;
	while (skb->len >= NLMSG_SPACE(sizeof(struct dumb)))
		nlh = nlmsg_hdr(skb);
		msg_len = NLMSG_ALIGN(nlh->nlmsg_len);

		skb_pull(skb, msg_len);
	}
	*/
}
#endif

static void dummy_nlk_fini(void);

static int __init dummy_nlk_init(void)
{
	DEBUGP(KERN_ALERT "[INFO] r_grps: %u\n", r_grps);
	nlsk = netlink_kernel_create(&init_net, NETLINK_TEST, r_grps,
		nlsk_recv, NULL, THIS_MODULE);
	if (!nlsk) {
		DEBUGP(KERN_ALERT "[ERR] netlink_kernel_create failed!\n");
		goto fail_init;
	}

	schedule_delayed_work(&writer_work, 10 * HZ); // to keventd
	return 0;
fail_init:
	dummy_nlk_fini();
	return -ENOMEM;
}

static void dummy_nlk_fini(void)
{
	cancel_delayed_work_sync(&writer_work);
	netlink_kernel_release(nlsk);
}

module_init(dummy_nlk_init);
module_exit(dummy_nlk_fini);
