#ifndef READ_H
#define READ_H

#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>

#include "config.h"
#include "para.h"
#include "utils/get_clock.h"

#define MAX_SEND_SGE (1)
#define MAIN_ALLOC(var, type, size, label)                       \
  {                                                              \
    if ((var = (type *)malloc(sizeof(type) * (size))) == NULL) { \
      fprintf(stderr, " Cannot Allocate\n");                     \
      goto label;                                                \
    }                                                            \
  }

#define NOTIFY_COMP_ERROR_SEND(wc, scnt, ccnt)                           \
  {                                                                      \
    fprintf(stderr, " Completion with error at client\n");               \
    fprintf(                                                             \
        stderr, " Failed status %d: wr_id %d syndrom 0x%x\n", wc.status, \
        (int)wc.wr_id, wc.vendor_err);                                   \
    fprintf(stderr, "scnt=%lu, ccnt=%lu\n", scnt, ccnt);                 \
  }

struct cma {
  struct rdma_event_channel *channel;
  struct rdma_addrinfo *rai;
  struct cma_node *nodes;
  int connection_index;
  int connects_left;
  int disconnects_left;
};

struct pingpong_context {
  struct cma cma_master;
  struct rdma_event_channel *cm_channel;
  struct rdma_cm_id *cm_id_control;
  struct rdma_cm_id *cm_id;
  struct ibv_context *context;
#ifdef HAVE_AES_XTS
  struct mlx5dv_mkey **mkey;
  struct mlx5dv_dek **dek;
#endif
  struct ibv_comp_channel *recv_channel;
  struct ibv_comp_channel *send_channel;
  struct ibv_pd *pd;
  struct ibv_mr **mr;
  struct ibv_mr *null_mr;
  struct ibv_cq *send_cq;
  struct ibv_cq *recv_cq;
  void **buf;
  struct ibv_ah **ah;
  struct ibv_qp **qp;
#ifdef HAVE_IBV_WR_API
  struct ibv_qp_ex **qpx;
#ifdef HAVE_MLX5DV
  struct mlx5dv_qp_ex **dv_qp;
#endif
  int (*new_post_send_work_request_func_pointer)(
      struct pingpong_context *ctx, int index,
      struct perftest_parameters *user_param);
#endif
  struct ibv_srq *srq;
  struct ibv_sge *sge_list;
  struct ibv_sge *recv_sge_list;
  struct ibv_send_wr *wr;
  struct ibv_recv_wr *rwr;
  uint64_t size;
  uint64_t *my_addr;
  uint64_t *rx_buffer_addr;
  uint64_t *rem_addr;
  uint32_t *rem_qpn;
  uint64_t buff_size;
  uint64_t send_qp_buff_size;
  uint64_t flow_buff_size;
  int tx_depth;
  uint64_t *scnt;
  uint64_t *ccnt;
  int is_contig_supported;
  uint32_t *r_dctn;
  uint32_t *dci_stream_id;
  int dek_number;
  uint32_t *ctrl_buf;
  uint32_t *credit_buf;
  struct ibv_mr *credit_mr;
  struct ibv_sge *ctrl_sge_list;
  struct ibv_send_wr *ctrl_wr;
  int send_rcredit;
  int credit_cnt;
  int cache_line_size;
  int cycle_buffer;
  int rposted;
  struct memory_ctx *memory;

  void print_para() {
    printf("size               \t%d\n", size);
    printf("buff_size          \t%d\n", buff_size);
    printf("send_qp_buff_size  \t%d\n", send_qp_buff_size);
    printf("flow_buff_size     \t%d\n", flow_buff_size);
    printf("tx_depth           \t%d\n", tx_depth);
    printf("is_contig_supported\t%d\n", is_contig_supported);
    printf("dek_number         \t%d\n", dek_number);
    printf("send_rcredit       \t%d\n", send_rcredit);
    printf("credit_cnt         \t%d\n", credit_cnt);
    printf("cache_line_size    \t%d\n", cache_line_size);
    printf("cycle_buffer       \t%d\n", cycle_buffer);
    printf("rposted            \t%d\n", rposted);
  }
};

struct pingpong_dest {
  int lid;
  int out_reads;
  int qpn;
  int psn;
  unsigned rkey;
  unsigned long long vaddr;
  union ibv_gid gid;
  unsigned srqn;
  int gid_index;
};

struct perftest_comm {
  struct pingpong_context *rdma_ctx;
  struct perftest_parameters *rdma_params;
};

struct ibv_device *ctx_find_dev(char **ib_devname) {
  int num_of_device;
  struct ibv_device **dev_list;
  struct ibv_device *ib_dev = NULL;

  dev_list = ibv_get_device_list(&num_of_device);

  // coverity[uninit_use]
  if (num_of_device <= 0) {
    fprintf(stderr, " Did not detect devices \n");
    fprintf(stderr, " If device exists, check if driver is up\n");
    return NULL;
  }

  if (!ib_devname) {
    fprintf(stderr, " Internal error, existing.\n");
    return NULL;
  }

  if (!*ib_devname) {
    ib_dev = dev_list[0];
    if (!ib_dev) {
      fprintf(stderr, "No IB devices found\n");
      exit(1);
    }
  } else {
    for (; (ib_dev = *dev_list); ++dev_list)
      if (!strcmp(ibv_get_device_name(ib_dev), *ib_devname)) break;
    if (!ib_dev) {
      fprintf(stderr, "IB device %s not found\n", *ib_devname);
      return NULL;
    }
  }

  GET_STRING(*ib_devname, ibv_get_device_name(ib_dev));
  return ib_dev;
}

int check_add_port(
    char **service, int port, const char *servername, struct addrinfo *hints,
    struct addrinfo **res) {
  int number;
  if (asprintf(service, "%d", port) < 0) {
    return FAILURE;
  }
  number = getaddrinfo(servername, *service, hints, res);
  free(*service);
  if (number < 0) {
    fprintf(
        stderr, "%s for ai_family: %x service: %s port: %d\n",
        gai_strerror(number), hints->ai_family, servername, port);
    return FAILURE;
  }
  return SUCCESS;
}

static int ethernet_client_connect(struct perftest_comm *comm) {
  struct addrinfo *res, *t;
  struct addrinfo hints;
  char *service;
  struct sockaddr_in source;

  int sockfd = -1;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = comm->rdma_params->ai_family;
  hints.ai_socktype = SOCK_STREAM;

  // if (comm->rdma_params->has_source_ip) {
  //   memset(&source, 0, sizeof(source));
  //   source.sin_family = AF_INET;
  //   source.sin_addr.s_addr = inet_addr(comm->rdma_params->source_ip);
  // }

  if (check_add_port(
          &service, comm->rdma_params->port, comm->rdma_params->servername,
          &hints, &res)) {
    fprintf(stderr, "Problem in resolving basic address and port\n");
    return 1;
  }

  for (t = res; t; t = t->ai_next) {
    sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
    if (sockfd >= 0) {
      // if (comm->rdma_params->has_source_ip) {
      //   if (bind(sockfd, (struct sockaddr *)&source, sizeof(source)) < 0) {
      //     fprintf(stderr, "Failed to bind socket\n");
      //     close(sockfd);
      //     return 1;
      //   }
      // }
      if (!connect(sockfd, t->ai_addr, t->ai_addrlen)) break;
      close(sockfd);
      sockfd = -1;
    }
  }

  freeaddrinfo(res);

  if (sockfd < 0) {
    fprintf(
        stderr, "Couldn't connect to %s:%d\n", comm->rdma_params->servername,
        comm->rdma_params->port);
    return 1;
  }

  comm->rdma_params->sockfd = sockfd;
  return 0;
}

static int ethernet_server_connect(struct perftest_comm *comm) {
  struct addrinfo *res, *t;
  struct addrinfo hints;
  char *service;
  int n;
  int sockfd = -1, connfd;
  // char *src_ip =
  //     comm->rdma_params->has_source_ip ? comm->rdma_params->source_ip : NULL;
  char *src_ip = NULL;

  memset(&hints, 0, sizeof hints);
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = comm->rdma_params->ai_family;
  hints.ai_socktype = SOCK_STREAM;

  if (check_add_port(&service, comm->rdma_params->port, src_ip, &hints, &res)) {
    fprintf(stderr, "Problem in resolving basic address and port\n");
    return 1;
  }

  for (t = res; t; t = t->ai_next) {
    if (t->ai_family != comm->rdma_params->ai_family) continue;

    sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);

    if (sockfd >= 0) {
      n = 1;
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);
      if (!bind(sockfd, t->ai_addr, t->ai_addrlen)) break;
      close(sockfd);
      sockfd = -1;
    }
  }
  freeaddrinfo(res);

  if (sockfd < 0) {
    fprintf(stderr, "Couldn't listen to port %d\n", comm->rdma_params->port);
    return 1;
  }

  listen(sockfd, 1);
  connfd = accept(sockfd, NULL, 0);

  if (connfd < 0) {
    perror("server accept");
    fprintf(stderr, "accept() failed\n");
    close(sockfd);
    return 1;
  }
  close(sockfd);
  comm->rdma_params->sockfd = connfd;
  return 0;
}

int establish_connection(struct perftest_comm *comm) {
  // printf("establish_connection\n");
  int (*ptr)(struct perftest_comm *);
  ptr = comm->rdma_params->servername ? &ethernet_client_connect
                                      : &ethernet_server_connect;

  if ((*ptr)(comm)) {
    fprintf(stderr, "Unable to open file descriptor for socket connection\n");
    return 1;
  }
  return 0;
}

// void exchange_versions(
//     struct perftest_comm *user_comm, struct perftest_parameters *user_param)
//     {
//   // printf("exchange_versions\n");
//   // if (ctx_xchg_data(user_comm, (void *)(&user_param->version),
//   //                   (void *)(&user_param->rem_version),
//   //                   sizeof(user_param->rem_version))) {
//   //   fprintf(stderr, " Failed to exchange data between server and
//   clients\n");
//   //   exit(1);
//   // }
// }

void dealloc_comm_struct(
    struct perftest_comm *comm, struct perftest_parameters *user_param) {
  free(comm->rdma_params);
}

enum ibv_mtu set_mtu(
    struct ibv_context *context, uint8_t ib_port, int user_mtu) {
  struct ibv_port_attr port_attr;
  enum ibv_mtu curr_mtu;

  if (ibv_query_port(context, ib_port, &port_attr)) {
    fprintf(stderr, " Error when trying to query port\n");
    exit(1);
  }

  /* User did not ask for specific mtu. */
  if (user_mtu == 0) {
    enum ctx_device current_dev = ib_dev_name(context);
    curr_mtu = port_attr.active_mtu;
  }

  else {
    switch (user_mtu) {
      case 256:
        curr_mtu = IBV_MTU_256;
        break;
      case 512:
        curr_mtu = IBV_MTU_512;
        break;
      case 1024:
        curr_mtu = IBV_MTU_1024;
        break;
      case 2048:
        curr_mtu = IBV_MTU_2048;
        break;
      case 4096:
        curr_mtu = IBV_MTU_4096;
        break;
      default:
        fprintf(stderr, " Invalid MTU - %d \n", user_mtu);
        fprintf(stderr, " Please choose mtu from {256,512,1024,2048,4096}\n");
        // coverity[uninit_use_in_call]
        fprintf(
            stderr, " Will run with the port active mtu - %d\n",
            port_attr.active_mtu);
        curr_mtu = port_attr.active_mtu;
    }

    if (curr_mtu > port_attr.active_mtu) {
      fprintf(stdout, "Requested mtu is higher than active mtu \n");
      fprintf(stdout, "Changing to active mtu - %d\n", port_attr.active_mtu);
      curr_mtu = port_attr.active_mtu;
    }
  }
  return curr_mtu;
}

int check_mtu(
    struct ibv_context *context, struct perftest_parameters *user_param,
    struct perftest_comm *user_comm) {
  // printf("check_mtu\n");
  int curr_mtu, rem_mtu;
  char cur[sizeof(int)];
  char rem[sizeof(int)];
  int size_of_cur;

  curr_mtu = (int)(set_mtu(context, user_param->ib_port, user_param->mtu));
  user_param->curr_mtu = (enum ibv_mtu)(curr_mtu);
  return SUCCESS;
}

void dealloc_ctx(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  if (user_param->port_by_qp != NULL) free(user_param->port_by_qp);

  if (ctx->qp != NULL) free(ctx->qp);

  if (ctx->mr != NULL) free(ctx->mr);
  if (ctx->buf != NULL) free(ctx->buf);

  if (ctx->sge_list != NULL) free(ctx->sge_list);
  if (ctx->wr != NULL) free(ctx->wr);
  if (ctx->rem_qpn != NULL) free(ctx->rem_qpn);

  if (ctx->memory != NULL) {
    ctx->memory = NULL;
  }
}

/* Macro for allocating in alloc_ctx function */
#define ALLOC(var, type, size)                                   \
  {                                                              \
    if ((var = (type *)malloc(sizeof(type) * (size))) == NULL) { \
      fprintf(stderr, " Cannot Allocate\n");                     \
      dealloc_ctx(ctx, user_param);                              \
      return 1;                                                  \
    }                                                            \
  }

int alloc_ctx(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  uint64_t tarr_size;
  int num_of_qps_factor;
  ctx->cycle_buffer = user_param->cycle_buffer;
  ctx->cache_line_size = user_param->cache_line_size;

  ALLOC(user_param->port_by_qp, uint64_t, user_param->num_of_qps);

  tarr_size = user_param->iters * user_param->num_of_qps;
  ALLOC(user_param->tposted, cycles_t, tarr_size);
  memset(user_param->tposted, 0, sizeof(cycles_t) * tarr_size);
  ALLOC(user_param->tcompleted, cycles_t, 1);

  ALLOC(ctx->qp, struct ibv_qp *, user_param->num_of_qps);

  ALLOC(ctx->mr, struct ibv_mr *, user_param->num_of_qps);
  ALLOC(ctx->buf, void *, user_param->num_of_qps);

  if (user_param->machine == CLIENT || true) {
    ALLOC(
        ctx->sge_list, struct ibv_sge,
        user_param->num_of_qps * user_param->post_list);
    ALLOC(
        ctx->wr, struct ibv_send_wr,
        user_param->num_of_qps * user_param->post_list);
    ALLOC(ctx->rem_qpn, uint32_t, user_param->num_of_qps);
    // ALLOC(ctx->ah, struct ibv_ah *, user_param->num_of_qps);
  }

  ctx->size = user_param->size;

  num_of_qps_factor = user_param->num_of_qps;

  /* holds the size of maximum between msg size and cycle buffer,
   * aligned to cache line, it is multiplied by 2 to be used as
   * send buffer(first half) and receive buffer(second half)
   * with reference to number of flows and number of QPs
   */
  ctx->buff_size =
      INC(BUFF_SIZE(ctx->size, ctx->cycle_buffer), ctx->cache_line_size) * 2 *
      num_of_qps_factor * user_param->flows;
  ctx->send_qp_buff_size = ctx->buff_size / num_of_qps_factor / 2;
  ctx->flow_buff_size = ctx->send_qp_buff_size / user_param->flows;
  user_param->buff_size = ctx->buff_size;

  ctx->memory = user_param->memory_create(user_param);  // need fix

  return SUCCESS;
}

int create_single_mr(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int qp_index) {
  int flags = IBV_ACCESS_LOCAL_WRITE;
  bool can_init_mem = true;
  int dmabuf_fd = 0;
  uint64_t dmabuf_offset;

  if (ctx->is_contig_supported == SUCCESS) {
    ctx->buf[qp_index] = NULL;
    flags |= (1 << 5);
  } else if (ctx->memory->allocate_buffer(
                 ctx->memory, user_param->cycle_buffer, ctx->buff_size,
                 &dmabuf_fd, &dmabuf_offset, &ctx->buf[qp_index],
                 &can_init_mem)) {
    return FAILURE;
  }

  flags |= IBV_ACCESS_REMOTE_READ;
  if (user_param->transport_type == IBV_TRANSPORT_IWARP)
    flags |= IBV_ACCESS_REMOTE_WRITE;

  {
    ctx->mr[qp_index] =
        ibv_reg_mr(ctx->pd, ctx->buf[qp_index], ctx->buff_size, flags);
    if (!ctx->mr[qp_index]) {
      fprintf(stderr, "Couldn't allocate MR\n");
      return FAILURE;
    }
  }

  if (ctx->is_contig_supported == SUCCESS)
    ctx->buf[qp_index] = ctx->mr[qp_index]->addr;

  /* Initialize buffer with random numbers except in WRITE_LAT test that it 0's
   */
  if (can_init_mem) {
    srand(time(NULL));
    uint64_t i;
    for (i = 0; i < ctx->buff_size; i++) {
      ((char *)ctx->buf[qp_index])[i] = (char)rand();
    }
  }
  return SUCCESS;
}

int create_mr(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  int i;
  int mr_index = 0;

  /* create first MR */
  if (create_single_mr(ctx, user_param, 0)) {
    fprintf(stderr, "failed to create mr\n");
    return 1;
  }
  mr_index++;

  /* create the rest if needed, or copy the first one */
  for (i = 1; i < user_param->num_of_qps; i++) {
    ctx->mr[i] = ctx->mr[0];
    // cppcheck-suppress arithOperationsOnVoidPointer
    ctx->buf[i] =
        (char *)ctx->buf[0] + (i * BUFF_SIZE(ctx->size, ctx->cycle_buffer));
  }

  return 0;

destroy_mr:
  for (i = 0; i < mr_index; i++) ibv_dereg_mr(ctx->mr[i]);

  return FAILURE;
}

int create_reg_cqs(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int tx_buffer_depth, int need_recv_cq) {
  ctx->send_cq = ibv_create_cq(
      ctx->context, tx_buffer_depth * user_param->num_of_qps, NULL,
      ctx->send_channel, 0);
  if (!ctx->send_cq) {
    fprintf(stderr, "Couldn't create CQ\n");
    return FAILURE;
  }

  return SUCCESS;
}

int create_cqs(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  int ret;
  int dct_only = 0, need_recv_cq = 0;
  int tx_buffer_depth = user_param->tx_depth;

  ret = create_reg_cqs(ctx, user_param, tx_buffer_depth, need_recv_cq);

  return ret;
}

struct ibv_qp *ctx_qp_create(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int qp_index) {
  struct ibv_qp *qp = NULL;
  int dc_num_of_qps = user_param->num_of_qps / 2;

  int is_dc_server_side = 0;
  struct ibv_qp_init_attr attr;
  memset(&attr, 0, sizeof(struct ibv_qp_init_attr));
  struct ibv_qp_cap *qp_cap = &attr.cap;

  attr.send_cq = ctx->send_cq;
  attr.recv_cq = ctx->send_cq;

  attr.cap.max_inline_data = user_param->inline_size;
  attr.cap.max_send_wr = user_param->tx_depth;
  attr.cap.max_send_sge = MAX_SEND_SGE;

  attr.srq = NULL;
  attr.cap.max_recv_wr = user_param->rx_depth;
  attr.cap.max_recv_sge = MAX_RECV_SGE;

  attr.qp_type = IBV_QPT_RC;

  qp = ibv_create_qp(ctx->pd, &attr);

  if (qp == NULL && errno == ENOMEM) {
    fprintf(
        stderr,
        "Requested QP size might be too big. Try reducing TX depth "
        "and/or inline size.\n");
    fprintf(
        stderr, "Current TX depth is %d and inline size is %d .\n",
        user_param->tx_depth, user_param->inline_size);
  }

  if (user_param->inline_size > qp_cap->max_inline_data) {
    printf(
        "  Actual inline-size(%d) < requested inline-size(%d)\n",
        qp_cap->max_inline_data, user_param->inline_size);
    user_param->inline_size = qp_cap->max_inline_data;
  }

  return qp;
}

int create_reg_qp_main(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int i) {
  ctx->qp[i] = ctx_qp_create(ctx, user_param, i);

  if (ctx->qp[i] == NULL) {
    fprintf(stderr, "Unable to create QP.\n");
    return FAILURE;
  }

  return SUCCESS;
}
int create_qp_main(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int i) {
  int ret;
  ret = create_reg_qp_main(ctx, user_param, i);
  return ret;
}

int ctx_modify_qp_to_init(
    struct ibv_qp *qp, struct perftest_parameters *user_param, int qp_index) {
  int num_of_qps = user_param->num_of_qps;
  int num_of_qps_per_port = user_param->num_of_qps / 2;

  struct ibv_qp_attr attr;
  int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT;
  int is_dc_server_side = 0;

  int ret = 0;

  memset(&attr, 0, sizeof(struct ibv_qp_attr));
  attr.qp_state = IBV_QPS_INIT;
  attr.pkey_index = user_param->pkey_index;

  attr.port_num = user_param->ib_port;
  attr.qp_access_flags = IBV_ACCESS_REMOTE_READ;
  flags |= IBV_QP_ACCESS_FLAGS;
  ret = ibv_modify_qp(qp, &attr, flags);

  if (ret) {
    fprintf(stderr, "Failed to modify QP to INIT, ret=%d\n", ret);
    return 1;
  }
  return 0;
}

int modify_qp_to_init(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    int qp_index) {
  if (ctx_modify_qp_to_init(ctx->qp[qp_index], user_param, qp_index)) {
    fprintf(stderr, "Failed to modify QP to INIT\n");
    return FAILURE;
  }

  return SUCCESS;
}

int ctx_init(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  // printf("ctx_init\n");
  int i;
  int dct_only = false;
  int qp_index = 0, dereg_counter;

  ctx->is_contig_supported = FAILURE;

  /* Allocating the Protection domain. */
  ctx->pd = ibv_alloc_pd(ctx->context);
  if (!ctx->pd) {
    fprintf(stderr, "Couldn't allocate PD\n");
    exit(-1);
  }

  if (ctx->memory->init(ctx->memory)) {
    fprintf(stderr, "Failed to init memory\n");
    goto mkey;
  }

  if (create_mr(ctx, user_param)) {
    fprintf(stderr, "Failed to create MR\n");
    goto mkey;
  }

  if (create_cqs(ctx, user_param)) {
    fprintf(stderr, "Failed to create CQs\n");
    goto mr;
  }

  // if (user_param->use_srq && user_param->connection_type == DC &&
  //     (user_param->tst == LAT || user_param->machine == SERVER ||
  //      user_param->duplex == ON)) {
  //   struct ibv_srq_init_attr_ex attr;
  //   memset(&attr, 0, sizeof(attr));
  //   attr.comp_mask = IBV_SRQ_INIT_ATTR_TYPE | IBV_SRQ_INIT_ATTR_PD;
  //   attr.attr.max_wr = user_param->rx_depth;
  //   attr.attr.max_sge = 1;
  //   attr.pd = ctx->pd;

  //   attr.srq_type = IBV_SRQT_BASIC;
  //   ctx->srq = ibv_create_srq_ex(ctx->context, &attr);
  //   if (!ctx->srq) {
  //     fprintf(stderr, "Couldn't create SRQ\n");
  //     goto xrc_srq;
  //   }
  // }

  // if (user_param->use_srq && user_param->connection_type != DC &&
  //     !user_param->use_xrc &&
  //     (user_param->tst == LAT || user_param->machine == SERVER ||
  //      user_param->duplex == ON)) {

  //   struct ibv_srq_init_attr attr = {
  //       .attr = {/* when using sreq, rx_depth sets the max_wr */
  //                .max_wr = user_param->rx_depth,
  //                .max_sge = 1}};
  //   ctx->srq = ibv_create_srq(ctx->pd, &attr);
  //   if (!ctx->srq) {
  //     fprintf(stderr, "Couldn't create SRQ\n");
  //     goto xrcd;
  //   }
  // }

  /*
   * QPs creation in RDMA CM flow will be done separately.
   * Unless, the function called with RDMA CM connection contexts,
   * need to verify the call with the existence of ctx->cm_id.
   */

  for (i = 0; i < user_param->num_of_qps; i++) {
    if (create_qp_main(ctx, user_param, i)) {
      fprintf(stderr, "Failed to create QP.\n");
      goto qps;
    }

    modify_qp_to_init(ctx, user_param, i);
    qp_index++;
  }

  return SUCCESS;

qps:
  for (i = 0; i < qp_index; i++) {
    ibv_destroy_qp(ctx->qp[i]);
  }

// cppcheck-suppress unusedLabelConfiguration
cqs:
  ibv_destroy_cq(ctx->send_cq);

mr:
  dereg_counter = 1;

  for (i = 0; i < dereg_counter; i++) ibv_dereg_mr(ctx->mr[i]);

mkey:
  ibv_dealloc_pd(ctx->pd);

  return FAILURE;
}

static int ctx_modify_qp_to_rtr(
    struct ibv_qp *qp, struct ibv_qp_attr *attr,
    struct perftest_parameters *user_param, struct pingpong_dest *dest,
    struct pingpong_dest *my_dest, int qp_index) {
  int num_of_qps = user_param->num_of_qps;
  int flags = IBV_QP_STATE;

  attr->qp_state = IBV_QPS_RTR;
  attr->ah_attr.src_path_bits = 0;
  attr->ah_attr.port_num = user_param->ib_port;

  attr->ah_attr.dlid = dest->lid;
  attr->ah_attr.sl = user_param->sl;
  attr->ah_attr.is_global = 0;

  attr->path_mtu = user_param->curr_mtu;
  attr->dest_qp_num = dest->qpn;
  attr->rq_psn = dest->psn;

  flags |= (IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN);

  attr->max_dest_rd_atomic = my_dest->out_reads;
  attr->min_rnr_timer = MIN_RNR_TIMER;

  flags |= (IBV_QP_MIN_RNR_TIMER | IBV_QP_MAX_DEST_RD_ATOMIC);

  return ibv_modify_qp(qp, attr, flags);
}

static int ctx_modify_qp_to_rts(
    struct ibv_qp *qp, struct ibv_qp_attr *attr,
    struct perftest_parameters *user_param, struct pingpong_dest *dest,
    struct pingpong_dest *my_dest) {
  int flags = IBV_QP_STATE;

  attr->qp_state = IBV_QPS_RTS;

  flags |= IBV_QP_SQ_PSN;
  attr->sq_psn = my_dest->psn;

  attr->timeout = user_param->qp_timeout;
  attr->retry_cnt = 7;
  attr->rnr_retry = 7;
  attr->max_rd_atomic = dest->out_reads;
  flags |=
      (IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
       IBV_QP_MAX_QP_RD_ATOMIC);

  return ibv_modify_qp(qp, attr, flags);
}

int ctx_connect(
    struct pingpong_context *ctx, struct pingpong_dest *dest,
    struct perftest_parameters *user_param, struct pingpong_dest *my_dest) {
  int i;
  struct ibv_qp_attr attr;
  int xrc_offset = 0;

  for (i = 0; i < user_param->num_of_qps; i++) {
    memset(&attr, 0, sizeof attr);

    // printf("modify qp to rtr\n");
    if (ctx_modify_qp_to_rtr(
            ctx->qp[i], &attr, user_param, &dest[xrc_offset + i], &my_dest[i],
            i)) {
      fprintf(stderr, "Failed to modify QP %d to RTR\n", ctx->qp[i]->qp_num);
      return FAILURE;
    }

    // printf("modify qp to rts\n");
    if (ctx_modify_qp_to_rts(
            ctx->qp[i], &attr, user_param, &dest[xrc_offset + i],
            &my_dest[i])) {
      fprintf(stderr, "Failed to modify QP to RTS\n");
      return FAILURE;
    }
  }
  return SUCCESS;
}

void ctx_set_send_reg_wqes(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    struct pingpong_dest *rem_dest) {
  int i, j;
  int num_of_qps = user_param->num_of_qps;
  int xrc_offset = 0;
  uint32_t remote_qkey;

  for (i = 0; i < num_of_qps; i++) {
    memset(&ctx->wr[i * user_param->post_list], 0, sizeof(struct ibv_send_wr));
    ctx->sge_list[i * user_param->post_list].addr = (uintptr_t)ctx->buf[i];

    ctx->wr[i * user_param->post_list].wr.rdma.remote_addr =
        rem_dest[xrc_offset + i].vaddr;

    for (j = 0; j < user_param->post_list; j++) {
      ctx->sge_list[i * user_param->post_list + j].length = user_param->size;
      ctx->sge_list[i * user_param->post_list + j].lkey = ctx->mr[i]->lkey;

      if (j > 0) {
        ctx->sge_list[i * user_param->post_list + j].addr =
            ctx->sge_list[i * user_param->post_list + (j - 1)].addr;
      }

      ctx->wr[i * user_param->post_list + j].sg_list =
          &ctx->sge_list[i * user_param->post_list + j];
      ctx->wr[i * user_param->post_list + j].num_sge = MAX_SEND_SGE;
      ctx->wr[i * user_param->post_list + j].wr_id = i;

      if (j == (user_param->post_list - 1)) {
        ctx->wr[i * user_param->post_list + j].next = NULL;
      } else {
        ctx->wr[i * user_param->post_list + j].next =
            &ctx->wr[i * user_param->post_list + j + 1];
      }

      if ((j + 1) % user_param->cq_mod == 0) {
        ctx->wr[i * user_param->post_list + j].send_flags = IBV_SEND_SIGNALED;
      } else {
        ctx->wr[i * user_param->post_list + j].send_flags = 0;
      }

      ctx->wr[i * user_param->post_list + j].opcode = IBV_WR_RDMA_READ;

      ctx->wr[i * user_param->post_list + j].wr.rdma.rkey =
          rem_dest[xrc_offset + i].rkey;
      if (j > 0) {
        ctx->wr[i * user_param->post_list + j].wr.rdma.remote_addr =
            ctx->wr[i * user_param->post_list + (j - 1)].wr.rdma.remote_addr;
      }
    }
  }
}

void ctx_set_send_wqes(
    struct pingpong_context *ctx, struct perftest_parameters *user_param,
    struct pingpong_dest *rem_dest) {
  ctx_set_send_reg_wqes(ctx, user_param, rem_dest);
}

static inline int post_send_method(
    struct pingpong_context *ctx, int index,
    struct perftest_parameters *user_param) {
  // ibv_wr_rdma_read();
  struct ibv_send_wr *bad_wr = NULL;
  return ibv_post_send(
      ctx->qp[index], &ctx->wr[index * user_param->post_list], &bad_wr);
}

int run_iter_lat(
    struct pingpong_context *ctx, struct perftest_parameters *user_param) {
  uint64_t scnt = 0;
  int ne;
  int err = 0;
  struct ibv_wc wc;
  int cpu_mhz = get_cpu_mhz(user_param->cpu_freq_f);
  int total_gap_cycles = 0;  // user_param->latency_gap * cpu_mhz;
  cycles_t end_cycle, start_gap;

  ctx->wr[0].sg_list->length = user_param->size;
  ctx->wr[0].send_flags = IBV_SEND_SIGNALED;

  while (scnt < user_param->iters) {
    // if (user_param->latency_gap) {
    //   start_gap = get_cycles();
    //   end_cycle = start_gap + total_gap_cycles;
    //   while (get_cycles() < end_cycle) {
    //     continue;
    //   }
    // }
    // if (user_param->test_type == ITERATIONS)
    user_param->tposted[scnt++] = get_cycles();

    err = post_send_method(ctx, 0, user_param);

    if (err) {
      fprintf(stderr, "Couldn't post send: scnt=%lu\n", scnt);
      return 1;
    }

    do {
      ne = ibv_poll_cq(ctx->send_cq, 1, &wc);
      if (ne > 0) {
        if (wc.status != IBV_WC_SUCCESS) {
          // coverity[uninit_use_in_call]
          NOTIFY_COMP_ERROR_SEND(wc, scnt, scnt);
          return 1;
        }

      } else if (ne < 0) {
        fprintf(stderr, "poll CQ failed %d\n", ne);
        return FAILURE;
      }

    } while (ne == 0);
  }

  return 0;
}

#endif  // READ_H