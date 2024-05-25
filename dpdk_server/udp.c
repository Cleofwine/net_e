

#include <stdio.h>
#include <rte_eal.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h>

#include <arpa/inet.h>

#define MBUF_COUNT		4096		
#define BURST_SIZE		32


#define ENABLE_SEND		1

// eth0 , 0
// eth1 , 1

int gDpdkPortId = 0;


static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};


#if ENABLE_SEND

static uint8_t src_mac[RTE_ETHER_ADDR_LEN]; 
static uint8_t dst_mac[RTE_ETHER_ADDR_LEN];

static uint32_t src_ip;
static uint32_t dst_ip;

static uint16_t src_port;
static uint16_t dst_port;


/*

udp pkt

-------------------------------------------------------------------
| rte_ether_hdr | rte_ipv4_hdr | rte_udp_hdr |        data        |
-------------------------------------------------------------------


rte_ether_hdr

-------------------------------------------------------------
|		src_mac			|		dst_mac			|	type	|
-------------------------------------------------------------
	
*/


static int encode_udp_pkt(uint8_t *msg, uint8_t *data, uint16_t length) {

	//1 ethhdr

	struct rte_ether_hdr *eth = (struct rte_ether_hdr*)msg;

	rte_memcpy(eth->s_addr.addr_bytes, src_mac, RTE_ETHER_ADDR_LEN);
	rte_memcpy(eth->d_addr.addr_bytes, dst_mac, RTE_ETHER_ADDR_LEN);
	eth->ether_type = htons(RTE_ETHER_TYPE_IPV4);


	//1 iphdr

	struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(msg + sizeof(struct rte_ether_hdr));
	ip->version_ihl = 0x45; //
	ip->type_of_service = 0x0;	
	ip->total_length = htons(length + sizeof(struct rte_udp_hdr) + sizeof(struct rte_ipv4_hdr));		/**< length of packet */
	ip->packet_id = 0;		/**< packet ID */
	ip->fragment_offset = 0;	/**< fragmentation offset */
	ip->time_to_live = 64;		/**< time to live */
	ip->next_proto_id = IPPROTO_UDP;		/**< protocol ID */
	ip->src_addr = src_ip;		
	ip->dst_addr = dst_ip;		/**< destination address */
	
	ip->hdr_checksum = 0;		/**< header checksum */
	ip->hdr_checksum = rte_ipv4_cksum(ip);
	
	printf("rte_ipv4_cksum\n");

	//1 udphdr
	
	struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(msg + sizeof(struct rte_ether_hdr) + sizeof(struct rte_ipv4_hdr));
	udp->src_port = src_port;
	udp->dst_port = dst_port;

	udp->dgram_len = htons(length + sizeof(struct rte_udp_hdr));

	rte_memcpy((uint8_t*)(udp+1), data, length);

	udp->dgram_cksum = 0;
	udp->dgram_cksum = rte_ipv4_udptcp_cksum(ip, udp);

	printf("rte_ipv4_udptcp_cksum\n");

	return 0;
}


static struct rte_mbuf *send_udp_pkt(struct rte_mempool *mbufpool, uint8_t *data, uint16_t length) {



	uint16_t total_len = length + sizeof(struct rte_ether_hdr) 
		+ sizeof(struct rte_ipv4_hdr) + sizeof(struct rte_udp_hdr);

	printf("total_len: %d\n", total_len);
	
	struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbufpool);
	if (!mbuf) {
		rte_exit(EXIT_FAILURE, "rte_pktmbuf_alloc");
	}
	mbuf->pkt_len = total_len;
	mbuf->data_len = total_len;

	uint8_t *msg = rte_pktmbuf_mtod(mbuf, uint8_t*);
	
	encode_udp_pkt(msg, data, length);

	return mbuf;

}

#endif

// mbuf_pool
// rx_queue, ringbuffer --> mbufs
// mbufs

int main(int argc, char *argv[]) {

	if (rte_eal_init(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Failed to init EAL\n");
	} //

	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbufpool", MBUF_COUNT, 0, 0, 
		RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());


	struct rte_eth_conf port_conf = port_conf_default;
	int num_rx_queues = 1; //
	int num_tx_queues = 0;
	rte_eth_dev_configure(gDpdkPortId, num_rx_queues, num_tx_queues, &port_conf);

	rte_eth_rx_queue_setup(gDpdkPortId, 0, 128, rte_eth_dev_socket_id(gDpdkPortId), 
		NULL, mbuf_pool);


	rte_eth_dev_start(gDpdkPortId);


	rte_eth_macaddr_get(gDpdkPortId, (struct rte_ether_addr *)src_mac);
	

	// tcp

	while (1) {

		struct rte_mbuf *mbufs[BURST_SIZE];
		unsigned num_recvd = rte_eth_rx_burst(gDpdkPortId, 0, mbufs, BURST_SIZE);
		if (num_recvd > BURST_SIZE) {
			rte_exit(EXIT_FAILURE, "Failed rte_eth_rx_burst\n");
		}
		
		unsigned i = 0;
		for (i = 0;i < num_recvd;i ++) {

			struct rte_ether_hdr *ehdr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr *);
			if (ehdr->ether_type == htons(RTE_ETHER_TYPE_IPV4)) {

				struct rte_ipv4_hdr *iphdr = rte_pktmbuf_mtod_offset(mbufs[i],struct rte_ipv4_hdr *, sizeof(struct rte_ether_hdr));

				if (iphdr->next_proto_id == IPPROTO_UDP) {

					// tcp/ip, udp/ip

					struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

					uint16_t length = ntohs(udphdr->dgram_len);
					*((char*)udphdr + length) = '\0';

					printf("data: %s, length: %d\n", (char*)(udphdr + 1), length);
#if ENABLE_SEND

					rte_memcpy(dst_mac, ehdr->s_addr.addr_bytes, RTE_ETHER_ADDR_LEN);
					//rte_memcpy(src_mac, ehdr->d_addr.addr_bytes, RTE_ETHER_ADDR_LEN);

					//src_ip = ntohs(iphdr->dst_addr);
					rte_memcpy(&src_ip, &iphdr->dst_addr, sizeof(uint32_t));
					rte_memcpy(&dst_ip, &iphdr->src_addr, sizeof(uint32_t));

					printf("rte_memcpy\n");
                    rte_memcpy(&src_port, &udphdr->dst_port, sizeof(uint16_t));
                    rte_memcpy(&dst_port, &udphdr->src_port, sizeof(uint16_t));
					
					// mbuf
					printf("rte_memcpy dst_port\n");
					
					struct rte_mbuf *txbuf = send_udp_pkt(mbuf_pool, (uint8_t*)(udphdr + 1), length - sizeof(struct rte_udp_hdr));

					rte_eth_tx_burst(gDpdkPortId, 0, &txbuf, 1);
					rte_pktmbuf_free(txbuf);
#endif

				}

			}
			
			
		}
	}
	

	//getchar();
	
	printf("hello ustack\n");

}




