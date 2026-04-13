#include "include/protocols.h"
#include "include/queue.h"
#include "include/lib.h"
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


int qsort_compare(const void *a, const void *b) {

	struct route_table_entry *a_rt = (struct route_table_entry *)a;
	struct route_table_entry *b_rt = (struct route_table_entry *)b;

	// si prefixul si masca sunt in big endian

	uint32_t a_prefix = ntohl(a_rt->prefix);
	uint32_t b_prefix = ntohl(b_rt->prefix);

	if (b_prefix < a_prefix) {
		return -1;
	} else if (b_prefix > a_prefix) {
		return 1;
	}

	uint32_t a_mask = ntohl(a_rt->mask);
	uint32_t b_mask = ntohl(b_rt->mask);

	if (b_mask < a_mask) {
		return -1;
	} else if (b_mask < a_mask) {
		return 1;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	char buf[MAX_PACKET_LEN];

	// Do not modify this line
	init(argv + 2, argc - 2);


	struct route_table_entry *r_table = malloc(sizeof(struct route_table_entry) * 80000);
	DIE(r_table == NULL, "Tabela de rutare nu a fost alocata - linia 14");

	struct arp_table_entry *arp_table = malloc(sizeof(struct arp_table_entry) * MAX_PACKET_LEN);
	DIE(arp_table == NULL, "Tabela arp nu a fost alocata - linia 17");

	// populez r_table si iau size ul
	int r_table_len = read_rtable(argv[1], r_table);

	// populez arp_table si iau size ul
	int arp_table_len = parse_arp_table("arp_table.txt", arp_table);

	// char *buffer = malloc(sizeof(char) * MAX_PACKET_LEN);
	// DIE(buffer == NULL, "Bufferul pentru pachete nu a fost alocat - linia 26");

	
	// sortez r table crescator dupa masca
	qsort(r_table, r_table_len, sizeof(struct route_table_entry), qsort_compare);
	
	while (1) {

		size_t interface;
		size_t len;

		interface = recv_from_any_link(buf, &len);
		DIE(interface < 0, "recv_from_any_links");

    // TODO: Implement the router forwarding logic

    /* Note that packets received are in network order,
		any header field which has more than 1 byte will need to be conerted to
		host order. For example, ntohs(eth_hdr->ether_type). The oposite is needed when
		sending a packet on the link, */

		
		struct ether_hdr *ethernet_packet = (struct ether_hdr *)buf;

		// verificare daca e IPv4
		// daca nu e il ingnor
		if (ntohs(ethernet_packet->ethr_type) != 0x800) {

			printf("Am dat drop la pachet nu e IPv4");
			continue;
		}

		// verific daca adresa ip este pentru unul din routerele mele
		struct ip_hdr *ip_buf = (struct ip_hdr *)(buf + 14);
		
		bool in_routers = false;
		for (int i = 0; i < ROUTER_NUM_INTERFACES; ++i) {
			
			if (ip_buf->dest_addr == inet_addr(get_interface_ip(i))) {
				in_routers = true;
				printf("Adresa ip este pentru unul din routerele mele");
				break;
			}
		}

		if (in_routers == true) {
			continue;
		}

		// verificare checksum
		uint16_t previous_checksum = ntohs(ip_buf->checksum);
		ip_buf->checksum = 0;
		uint16_t actual_checksum = checksum((uint16_t *) ip_buf, sizeof(struct ip_hdr));
		
		if (previous_checksum != actual_checksum) {
			printf("Checksumuri diferite");
			continue;
		}

		if (ip_buf->ttl <= 1) {
			printf("E timpul sa iesi!!!");
			continue;
		}

		ip_buf->ttl--;
		uint16_t new_checksum = checksum((uint16_t *) ip_buf, sizeof(struct ip_hdr));
		ip_buf->checksum = htons(new_checksum);

		// caut LPM

		// ineficient
		// uint32_t maxx = 0; // lungimea maxima a mastii
		// struct route_table_entry *best_route = NULL;

		// for (int i = 0; i < r_table_len; ++i) {
			
		// 	if ((ip_buf->dest_addr & r_table[i].mask) == r_table[i].prefix) {

		// 		if (ntohl(r_table[i].mask) > maxx) {
		// 			maxx = ntohl(r_table[i].mask);
		// 			best_route = &r_table[i];
		// 		}
		// 	}
		// }

		// eficient
		struct route_table_entry *best_route = NULL;

		// folosesc cautarea binara pentru eficienta

		int l = 0, r = r_table_len - 1, mid;

		uint32_t dest_addr = ntohl(ip_buf->dest_addr);

		while (l <= r) {
			mid = (l + r) / 2;

			uint32_t preifx = ntohl(r_table[mid].prefix);
			uint32_t mask = ntohl(r_table[mid].mask);

			if ((dest_addr & mask) == preifx) {
				best_route = &r_table[mid];

				// cautam cautarea dupa cea mai buna masca
				l = mid + 1;
			} else if (dest_addr > preifx) {
				l = mid + 1;
			} else {
				r = mid - 1;
			}
		}

		if (best_route == NULL) {
			printf("Nu exista ruta");
			continue;
		}
		
		struct arp_table_entry *next_mac = NULL;

		for (int i = 0; i < arp_table_len; ++i) {

			if (best_route->next_hop == arp_table[i].ip) {

				next_mac = &arp_table[i];
				break;
			}
		}

		if (next_mac == NULL) {
			printf("N am gasit adresa MAC de la urmatorul ruter");
			continue;
		}

		for (int j = 0; j < 6; ++j) {
			ethernet_packet->ethr_dhost[j] = next_mac->mac[j];
		}

		get_interface_mac(best_route->interface, ethernet_packet->ethr_shost);
		send_to_link(len, buf, best_route->interface);
	}
}

