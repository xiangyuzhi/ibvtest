#ifndef COMM_H
#define COMM_H

#include "context.h"
#include "parameter.h"
#include "utils/get_clock.h"

#define KEY_MSG_SIZE (59)      /* Message size without gid. */
#define KEY_MSG_SIZE_GID (108) /* Message size with gid (MGID as well). */
#define SYNC_SPEC_ID (5)
#define KEY_PRINT_FMT "%04x:%04x:%06x:%06x:%08x:%016llx:%08x"
#define KEY_PRINT_FMT_GID                                                      \
  "%04x:%04x:%06x:%06x:%08x:%016llx:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%" \
  "02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%08x:"

#define MAIN_ALLOC(var, type, size, label)                       \
  {                                                              \
    if ((var = (type *)malloc(sizeof(type) * (size))) == NULL) { \
      fprintf(stderr, " Cannot Allocate\n");                     \
      goto label;                                                \
    }                                                            \
  }

int create_comm_struct(struct rdma_comm *comm, rdma_parameter *user_param) {
  MAIN_ALLOC(comm->rdma_params, rdma_parameter, 1, return_error);
  memset(comm->rdma_params, 0, sizeof(rdma_parameter));

  // remember to update when add new parameter.
  comm->rdma_params->port = user_param->port;
  comm->rdma_params->ai_family = user_param->ai_family;
  comm->rdma_params->sockfd = -1;
  comm->rdma_params->gid_index = user_param->gid_index;
  comm->rdma_params->servername = user_param->servername;
  comm->rdma_params->machine = user_param->machine;
  comm->rdma_params->cycle_buffer = user_param->cycle_buffer;
  comm->rdma_params->memory_create = host_memory_create;
  comm->rdma_params->side = LOCAL;

  return SUCCESS;
return_error:
  return FAILURE;
}

static inline int ipv6_addr_v4mapped(const struct in6_addr *a) {
  return ((a->s6_addr32[0] | a->s6_addr32[1]) |
          (a->s6_addr32[2] ^ htonl(0x0000ffff))) == 0UL ||
         /* IPv4 encoded multicast addresses */
         (a->s6_addr32[0] == htonl(0xff0e0000) &&
          ((a->s6_addr32[1] | (a->s6_addr32[2] ^ htonl(0x0000ffff))) == 0UL));
}

static int get_best_gid_index(
    rdma_context *ctx, rdma_parameter *user_param, struct ibv_port_attr *attr,
    int port) {
  int gid_index = 0, i;
  union ibv_gid temp_gid, temp_gid_rival;
  int is_ipv4, is_ipv4_rival;

  for (i = 1; i < attr->gid_tbl_len; i++) {
    if (ibv_query_gid(ctx->context, port, gid_index, &temp_gid)) {
      return -1;
    }

    if (ibv_query_gid(ctx->context, port, i, &temp_gid_rival)) {
      return -1;
    }

    is_ipv4 = ipv6_addr_v4mapped((struct in6_addr *)temp_gid.raw);
    is_ipv4_rival = ipv6_addr_v4mapped((struct in6_addr *)temp_gid_rival.raw);

    if (is_ipv4_rival && !is_ipv4) gid_index = i;
  }
  return gid_index;
}

uint16_t ctx_get_local_lid(struct ibv_context *context, int port) {
  struct ibv_port_attr attr;

  if (ibv_query_port(context, port, &attr)) return 0;

  // coverity[uninit_use]
  return attr.lid;
}

int set_up_connection(
    rdma_context *ctx, rdma_parameter *user_param,
    struct message_context *my_dest) {
  union ibv_gid temp_gid;
  struct ibv_port_attr attr;

  srand48(getpid() * time(NULL));

  if (user_param->gid_index != -1) {
    if (ibv_query_port(ctx->context, user_param->ib_port, &attr)) return 0;

    user_param->gid_index =
        get_best_gid_index(ctx, user_param, &attr, user_param->ib_port);
    if (user_param->gid_index < 0) return -1;
    if (ibv_query_gid(
            ctx->context, user_param->ib_port, user_param->gid_index,
            &temp_gid))
      return -1;
  }

  for (int i = 0; i < user_param->num_of_qps; i++) {
    /*single-port case*/
    my_dest[i].lid = ctx_get_local_lid(ctx->context, user_param->ib_port);
    my_dest[i].gid_index = user_param->gid_index;

    my_dest[i].qpn = ctx->qp[i]->qp_num;
    my_dest[i].psn = lrand48() & 0xffffff;
    my_dest[i].rkey = ctx->mr[i]->rkey;

    /* Each qp gives his receive buffer address.*/
    my_dest[i].out_reads = user_param->out_reads;
    // my_dest[i].vaddr =
    //     (uintptr_t)ctx->buf[0] +
    //     (user_param->num_of_qps + i) * BUFF_SIZE(ctx->size,
    //     ctx->cycle_buffer);
    my_dest[i].vaddr = (uintptr_t)ctx->buf[0];

    memcpy(my_dest[i].gid.raw, temp_gid.raw, 16);
  }

  return 0;
}

static int ethernet_read_keys(
    struct message_context *rem_dest, struct rdma_comm *comm) {
  if (rem_dest->gid_index == -1) {
    int parsed;
    char msg[KEY_MSG_SIZE];

    if (read(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
      fprintf(stderr, "ethernet_read_keys: Couldn't read remote address\n");
      return 1;
    }

    parsed = sscanf(
        msg, KEY_PRINT_FMT, (unsigned int *)&rem_dest->lid,
        (unsigned int *)&rem_dest->out_reads, (unsigned int *)&rem_dest->qpn,
        (unsigned int *)&rem_dest->psn, &rem_dest->rkey, &rem_dest->vaddr,
        &rem_dest->srqn);

    if (parsed != 7) {
      // coverity[string_null]
      fprintf(stderr, "Couldn't parse line <%.*s>\n", (int)sizeof msg, msg);
      return 1;
    }

  } else {
    char msg[KEY_MSG_SIZE_GID];
    char *pstr = msg, *term;
    char tmp[120];
    int i;

    if (read(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
      fprintf(stderr, "ethernet_read_keys: Couldn't read remote address\n");
      return 1;
    }

    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->lid = (int)strtol(tmp, NULL, 16); /*LID*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->out_reads = (int)strtol(tmp, NULL, 16); /*OUT_READS*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->qpn = (int)strtol(tmp, NULL, 16); /*QPN*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->psn = (int)strtol(tmp, NULL, 16); /*PSN*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->rkey = (unsigned)strtoul(tmp, NULL, 16); /*RKEY*/

    pstr += term - pstr + 1;
    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;

    rem_dest->vaddr = strtoull(tmp, NULL, 16); /*VA*/

    for (i = 0; i < 15; ++i) {
      pstr += term - pstr + 1;
      term = strpbrk(pstr, ":");
      memcpy(tmp, pstr, term - pstr);
      tmp[term - pstr] = 0;

      rem_dest->gid.raw[i] = (unsigned char)strtoll(tmp, NULL, 16);
    }

    pstr += term - pstr + 1;

    strcpy(tmp, pstr);
    rem_dest->gid.raw[15] = (unsigned char)strtoll(tmp, NULL, 16);

    pstr += term - pstr + 4;

    term = strpbrk(pstr, ":");
    memcpy(tmp, pstr, term - pstr);
    tmp[term - pstr] = 0;
    rem_dest->srqn = (unsigned)strtoul(tmp, NULL, 16); /*SRQN*/
  }
  return 0;
}

static int ethernet_write_keys(
    struct message_context *my_dest, struct rdma_comm *comm) {
  if (my_dest->gid_index == -1) {
    char msg[KEY_MSG_SIZE];

    sprintf(
        msg, KEY_PRINT_FMT, my_dest->lid, my_dest->out_reads, my_dest->qpn,
        my_dest->psn, my_dest->rkey, my_dest->vaddr, my_dest->srqn);

    if (write(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
      perror("client write");
      fprintf(stderr, "Couldn't send local address\n");
      return 1;
    }

  } else {
    char msg[KEY_MSG_SIZE_GID];
    sprintf(
        msg, KEY_PRINT_FMT_GID, my_dest->lid, my_dest->out_reads, my_dest->qpn,
        my_dest->psn, my_dest->rkey, my_dest->vaddr, my_dest->gid.raw[0],
        my_dest->gid.raw[1], my_dest->gid.raw[2], my_dest->gid.raw[3],
        my_dest->gid.raw[4], my_dest->gid.raw[5], my_dest->gid.raw[6],
        my_dest->gid.raw[7], my_dest->gid.raw[8], my_dest->gid.raw[9],
        my_dest->gid.raw[10], my_dest->gid.raw[11], my_dest->gid.raw[12],
        my_dest->gid.raw[13], my_dest->gid.raw[14], my_dest->gid.raw[15],
        my_dest->srqn);

    if (write(comm->rdma_params->sockfd, msg, sizeof msg) != sizeof msg) {
      perror("client write");
      fprintf(stderr, "Couldn't send local address\n");
      return 1;
    }
  }

  return 0;
}

int ctx_hand_shake(
    struct rdma_comm *comm, struct message_context *my_dest,
    struct message_context *rem_dest) {
  // printf("ctx_hand_shake\n");
  int (*read_func_ptr)(struct message_context *, struct rdma_comm *);
  int (*write_func_ptr)(struct message_context *, struct rdma_comm *);

  read_func_ptr = &ethernet_read_keys;
  write_func_ptr = &ethernet_write_keys;

  rem_dest->gid_index = my_dest->gid_index;
  if (comm->rdma_params->servername) {
    if ((*write_func_ptr)(my_dest, comm)) {
      fprintf(stderr, " Unable to write to socket/rdma_cm\n");
      return 1;
    }
    if ((*read_func_ptr)(rem_dest, comm)) {
      fprintf(stderr, " Unable to read from socket/rdma_cm\n");
      return 1;
    }

    /*Server side will wait for the client side to reach the write function.*/
  } else {
    if ((*read_func_ptr)(rem_dest, comm)) {
      fprintf(stderr, " Unable to read to socket/rdma_cm\n");
      return 1;
    }
    if ((*write_func_ptr)(my_dest, comm)) {
      fprintf(stderr, " Unable to write from socket/rdma_cm\n");
      return 1;
    }
  }

  return 0;
}

int ctx_close_connection(
    struct rdma_comm *comm, struct message_context *my_dest,
    struct message_context *rem_dest) {
  /*Signal client is finished.*/
  if (ctx_hand_shake(comm, my_dest, rem_dest)) {
    return 1;
  }

  close(comm->rdma_params->sockfd);

  return 0;
}

void com_init(
    rdma_context &ctx, rdma_parameter &user_param, rdma_comm &user_comm,
    message_context *&my_dest, message_context *&rem_dest) {
  memset(&ctx, 0, sizeof(rdma_context));
  memset(&user_comm, 0, sizeof(rdma_comm));

  struct ibv_device *ib_dev = ctx_find_dev(&user_param.ib_devname);
  if (!ib_dev) {
    fprintf(stderr, " Unable to find the Infiniband/RoCE device\n");
    exit(0);
  }

  ctx.context = ctx_open_device(ib_dev, &user_param);
  if (!ctx.context) {
    fprintf(stderr, " Couldn't get context for the device\n");
    exit(0);
  }

  if (check_link(ctx.context, &user_param)) {
    fprintf(stderr, " Couldn't get context for the device\n");
    exit(0);
  }

  if (create_comm_struct(&user_comm, &user_param)) {
    fprintf(stderr, " Unable to create RDMA_CM resources\n");
    exit(0);
  }

  if (user_param.machine == SERVER) {
    printf("\n************************************\n");
    printf("* Waiting for client to connect... *\n");
    printf("************************************\n");
  }

  if (establish_connection(&user_comm)) {
    fprintf(stderr, " Unable to init the socket connection\n");
    exit(0);
  }

  // exchange_versions(&user_comm, &user_param);

  if (check_mtu(ctx.context, &user_param, &user_comm)) {
    fprintf(stderr, " Couldn't get context for the device\n");
    dealloc_comm_struct(&user_comm, &user_param);
    exit(0);
  }

  my_dest = (struct message_context *)malloc(
      sizeof(struct message_context) * (user_param.num_of_qps));
  memset(my_dest, 0, sizeof(struct message_context) * user_param.num_of_qps);
  rem_dest = (struct message_context *)malloc(
      sizeof(struct message_context) * (user_param.num_of_qps));
  memset(rem_dest, 0, sizeof(struct message_context) * user_param.num_of_qps);

  if (alloc_ctx(&ctx, &user_param)) {
    fprintf(stderr, "Couldn't allocate context\n");
    exit(0);
  }

  if (ctx_init(&ctx, &user_param)) {
    fprintf(stderr, " Couldn't create IB resources\n");
    dealloc_ctx(&ctx, &user_param);
    free(rem_dest);
    exit(0);
  }

  /* Set up the Connection. */
  if (set_up_connection(&ctx, &user_param, my_dest)) {
    fprintf(stderr, " Unable to set up socket connection\n");
    exit(0);
  }

  for (int i = 0; i < user_param.num_of_qps; i++) {
    /* shaking hands and gather the other side info. */
    if (ctx_hand_shake(&user_comm, &my_dest[i], &rem_dest[i])) {
      fprintf(stderr, "Failed to exchange data between server and clients\n");
      exit(0);
    }
  }

  if (ctx_connect(&ctx, rem_dest, &user_param, my_dest)) {
    fprintf(stderr, " Unable to Connect the HCA's through the link\n");
    exit(-1);
  }

  user_comm.rdma_params->side = REMOTE;

  for (int i = 0; i < user_param.num_of_qps; i++) {
    if (ctx_hand_shake(&user_comm, &my_dest[i], &rem_dest[i])) {
      fprintf(stderr, " Failed to exchange data between server and clients\n");
      exit(-1);
    }
  }

  if (user_param.machine == SERVER) {
    if (ctx_close_connection(&user_comm, my_dest, rem_dest)) {
      fprintf(stderr, "Failed to close connection between server and client\n");
      free(rem_dest);
      exit(-1);
    }

    free(rem_dest);
    free(my_dest);
    free(user_param.ib_devname);
    free(user_comm.rdma_params);
    exit(-1);
  }
}

#endif