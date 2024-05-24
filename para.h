
#ifndef PARA_H
#define PARA_H

#include <getopt.h>
#include <infiniband/verbs.h>
#include <math.h>
#include <rdma/rdma_cma.h>

#include <iostream>

#include "mem.h"
#include "utils/get_clock.h"

#define RESULT_LINE                                                            \
  "--------------------------------------------------------------------------" \
  "-------------\n"
#define RESULT_FMT_LAT                                                     \
  " #bytes #iterations    t_min[usec]    t_max[usec]  t_typical[usec]    " \
  "t_avg[usec]    t_stdev[usec]   99"                                      \
  "%"                                                                      \
  " percentile[usec]   99.9"                                               \
  "%"                                                                      \
  " percentile[usec] "

#define RESULT_EXT "\n"

#define OFF (0)
#define ON (1)
#define DEF_PORT (18515)
#define DEF_IB_PORT (1)
#define LINK_UNSPEC (-2)
#define DEF_SIZE_LAT (2)
#define DEF_ITERS (1000)
#define DEF_GID_INDEX (-1)
#define DEF_INLINE (-1)
#define DEF_NUM_QPS (1)
#define DEF_FLOWS (1)
#define DEF_TX_LAT (1)
#define DEF_RX_RDMA (1)
#define MAX_RECV_SGE (1)
#define MIN_RNR_TIMER (12)
#define DEF_QP_TIME (14)
#define DEF_CQ_MOD (100)

/* Macro to define the buffer size (according to "Nahalem" chip set).
 * for small message size (under 4K) , we allocate 4K buffer , and the RDMA
 * write verb will write in cycle on the buffer. this improves the BW in
 * "Nahalem" systems.
 */
#define BUFF_SIZE(size, cycle_buffer) \
  ((size < cycle_buffer) ? (cycle_buffer) : (size))

#define ROUND_UP(value, alignment)        \
  (((value) % (alignment) == 0) ? (value) \
                                : ((alignment) * ((value) / (alignment) + 1)))

/* Macro that defines the address where we write in RDMA.
 * If message size is smaller then CACHE_LINE size then we write in CACHE_LINE
 * jumps.
 */
#define INC(size, cache_line_size)                            \
  ((size > cache_line_size) ? ROUND_UP(size, cache_line_size) \
                            : (cache_line_size))

#define GET_STRING(orig, temp)                \
  {                                           \
    ALLOCATE(orig, char, (strlen(temp) + 1)); \
    strcpy(orig, temp);                       \
  }

#define CHECK_VALUE(arg, type, name, not_int_ptr)                             \
  {                                                                           \
    arg = (type)strtol(optarg, &not_int_ptr, 0);                              \
    if (*not_int_ptr != '\0') /*not integer part is not empty*/ {             \
      fprintf(stderr, " %s argument %s should be %s\n", name, optarg, #type); \
      return 1;                                                               \
    }                                                                         \
  }

static const char *portStates[] = {"Nop",   "Down", "Init",
                                   "Armed", "",     "Active Defer"};

/* The type of the device */
enum ctx_device {
  DEVICE_ERROR = -1,
  UNKNOWN = 0,
  CONNECTX = 1,
  CONNECTX2 = 2,
  CONNECTX3 = 3,
  CONNECTIB = 4,
  LEGACY = 5,
  CHELSIO_T4 = 6,
  CHELSIO_T5 = 7,
  CONNECTX3_PRO = 8,
  SKYHAWK = 9,
  CONNECTX4 = 10,
  CONNECTX4LX = 11,
  QLOGIC_E4 = 12,
  QLOGIC_AH = 13,
  CHELSIO_T6 = 14,
  CONNECTX5 = 15,
  CONNECTX5EX = 16,
  CONNECTX6 = 17,
  CONNECTX6DX = 18,
  MLX5GENVF = 19,
  BLUEFIELD = 20,
  BLUEFIELD2 = 21,
  INTEL_GEN1 = 22,
  NETXTREME = 23,
  EFA = 24,
  CONNECTX6LX = 25,
  CONNECTX7 = 26,
  QLOGIC_AHP = 27,
  BLUEFIELD3 = 28,
  ERDMA = 29,
  HNS = 30,
  CONNECTX8 = 31,
  INTEL_GEN2 = 32,
};

typedef enum { SERVER, CLIENT, UNCHOSEN } MachineType;
typedef enum { LOCAL, REMOTE } PrintDataSide;

struct perftest_parameters {
  int port;
  char *ib_devname;
  char *servername;
  uint8_t ib_port;
  int mtu;
  enum ibv_mtu curr_mtu;
  uint64_t size;
  int req_size;
  uint64_t dct_key;
  uint64_t iters;
  int8_t link_type;
  enum ibv_transport_type transport_type;
  int gid_index;
  int inline_size;
  int out_reads;
  int pkey_index;
  MachineType machine;
  int ai_family;
  int sockfd;
  int num_of_qps;
  int cycle_buffer;
  int cache_line_size;
  uint64_t *port_by_qp;
  int post_list;
  int flows;
  int buff_size;
  struct memory_ctx *(*memory_create)(struct perftest_parameters *params);
  int tx_depth;
  int rx_depth;
  uint8_t sl;
  uint8_t qp_timeout;
  int cq_mod;
  PrintDataSide side;
  int cpu_freq_f;
  cycles_t *tposted;
  cycles_t *tcompleted;
};

static void init_perftest_params(struct perftest_parameters *user_param) {
  user_param->port = DEF_PORT;
  user_param->ib_port = DEF_IB_PORT;
  user_param->size = DEF_SIZE_LAT;
  user_param->req_size = 0;
  user_param->iters = DEF_ITERS;
  user_param->link_type = LINK_UNSPEC;
  user_param->gid_index = DEF_GID_INDEX;
  user_param->inline_size = DEF_INLINE;
  user_param->pkey_index = 0;
  user_param->ai_family = AF_INET;
  user_param->num_of_qps = DEF_NUM_QPS;
  user_param->cycle_buffer = sysconf(_SC_PAGESIZE);
  user_param->cache_line_size = get_cache_line_size();
  user_param->post_list = 1;
  user_param->flows = DEF_FLOWS;
  user_param->memory_create = host_memory_create;
  user_param->tx_depth = DEF_TX_LAT;
  user_param->rx_depth = DEF_RX_RDMA;
  user_param->qp_timeout = DEF_QP_TIME;
  user_param->cq_mod = DEF_CQ_MOD;
  user_param->cpu_freq_f = OFF;
}

int parser(struct perftest_parameters *user_param, char *argv[], int argc) {
  int c, size_len;
  char *server_ip = NULL;
  char *client_ip = NULL;
  char *not_int_ptr = NULL;

  init_perftest_params(user_param);

  while (1) {
    int long_option_index = -1;
    static const struct option long_options[] = {
        {.name = "port", .has_arg = 1, .val = 'p'},
        {.name = "ib-dev", .has_arg = 1, .val = 'd'},
        {.name = "ib-port", .has_arg = 1, .val = 'i'},
        {.name = "mtu", .has_arg = 1, .val = 'm'},
        {.name = "size", .has_arg = 1, .val = 's'},
        {.name = "iters", .has_arg = 1, .val = 'n'},
        {0}};
    c = getopt_long(
        argc, argv, "p:d:i:m:s:n", long_options, &long_option_index);
    if (c == -1) break;
    switch (c) {
      case 'p':
        CHECK_VALUE(user_param->port, int, "Port", not_int_ptr);
        break;
      case 'd':
        GET_STRING(user_param->ib_devname, strdupa(optarg));
        break;
    }
  }

  if (optind == argc - 1) {
    GET_STRING(user_param->servername, strdupa(argv[optind]));
  } else if (optind < argc) {
    fprintf(stderr, " Invalid Command line. Please check command rerun \n");
    return 1;
  }

  user_param->machine = user_param->servername ? CLIENT : SERVER;

  return 0;
}

struct ibv_context *ctx_open_device(
    struct ibv_device *ib_dev, struct perftest_parameters *user_param) {
  struct ibv_context *context;
  context = ibv_open_device(ib_dev);

  if (!context) {
    fprintf(stderr, " Couldn't get context for the device\n");
    return NULL;
  }

  return context;
}

const char *link_layer_str(int8_t link_layer) {
  switch (link_layer) {
    case IBV_LINK_LAYER_UNSPECIFIED:
    case IBV_LINK_LAYER_INFINIBAND:
      return "IB";
    case IBV_LINK_LAYER_ETHERNET:
      return "Ethernet";
    default:
      return "Unknown";
  }
}

enum ctx_device ib_dev_name(struct ibv_context *context) {
  enum ctx_device dev_fname = UNKNOWN;
  struct ibv_device_attr attr;

  if (ibv_query_device(context, &attr)) {
    dev_fname = DEVICE_ERROR;
  }

  else if (attr.vendor_id == 5157) {
    switch (attr.vendor_part_id >> 12) {
      case 10:
      case 4:
        dev_fname = CHELSIO_T4;
        break;
      case 11:
      case 5:
        dev_fname = CHELSIO_T5;
        break;
      case 6:
        dev_fname = CHELSIO_T6;
        break;
      default:
        dev_fname = UNKNOWN;
        break;
    }

    /* Assuming it's Mellanox HCA or unknown.
    If you want Inline support in other vendor devices, please send patch to
    gilr@dev.mellanox.co.il
    */
  } else if (attr.vendor_id == 0x8086) {
    switch (attr.vendor_part_id) {
      case 14289:
        dev_fname = INTEL_GEN1;
        break;
      case 5522:
        dev_fname = INTEL_GEN2;
        break;
      default:
        dev_fname = INTEL_GEN2;
        break;
    }
  } else {
    // coverity[uninit_use]
    switch (attr.vendor_part_id) {
      case 4099:
        dev_fname = CONNECTX3;
        break;
      case 4100:
        dev_fname = CONNECTX3;
        break;
      case 4103:
        dev_fname = CONNECTX3_PRO;
        break;
      case 4104:
        dev_fname = CONNECTX3_PRO;
        break;
      case 4113:
        dev_fname = CONNECTIB;
        break;
      case 4115:
        dev_fname = CONNECTX4;
        break;
      case 4116:
        dev_fname = CONNECTX4;
        break;
      case 4117:
        dev_fname = CONNECTX4LX;
        break;
      case 4118:
        dev_fname = CONNECTX4LX;
        break;
      case 4119:
        dev_fname = CONNECTX5;
        break;
      case 4120:
        dev_fname = CONNECTX5;
        break;
      case 4121:
        dev_fname = CONNECTX5EX;
        break;
      case 4122:
        dev_fname = CONNECTX5EX;
        break;
      case 4123:
        dev_fname = CONNECTX6;
        break;
      case 4124:
        dev_fname = CONNECTX6;
        break;
      case 4125:
        dev_fname = CONNECTX6DX;
        break;
      case 4126:
        dev_fname = MLX5GENVF;
        break;
      case 4127:
        dev_fname = CONNECTX6LX;
        break;
      case 4129:
        dev_fname = CONNECTX7;
        break;
      case 4131:
        dev_fname = CONNECTX8;
        break;
      case 41682:
        dev_fname = BLUEFIELD;
        break;
      case 41683:
        dev_fname = BLUEFIELD;
        break;
      case 41686:
        dev_fname = BLUEFIELD2;
        break;
      case 41692:
        dev_fname = BLUEFIELD3;
        break;
      case 26418:
        dev_fname = CONNECTX2;
        break;
      case 26428:
        dev_fname = CONNECTX2;
        break;
      case 26438:
        dev_fname = CONNECTX2;
        break;
      case 26448:
        dev_fname = CONNECTX2;
        break;
      case 26458:
        dev_fname = CONNECTX2;
        break;
      case 26468:
        dev_fname = CONNECTX2;
        break;
      case 26478:
        dev_fname = CONNECTX2;
        break;
      case 25408:
        dev_fname = CONNECTX;
        break;
      case 25418:
        dev_fname = CONNECTX;
        break;
      case 25428:
        dev_fname = CONNECTX;
        break;
      case 25448:
        dev_fname = CONNECTX;
        break;
      case 1824:
        dev_fname = SKYHAWK;
        break;
      case 5684:
        dev_fname = QLOGIC_E4;
        break;
      case 5700:
        dev_fname = QLOGIC_E4;
        break;
      case 5716:
        dev_fname = QLOGIC_E4;
        break;
      case 5718:
        dev_fname = QLOGIC_E4;
        break;
      case 5734:
        dev_fname = QLOGIC_E4;
        break;
      case 32880:
        dev_fname = QLOGIC_AH;
        break;
      case 32881:
        dev_fname = QLOGIC_AH;
        break;
      case 32882:
        dev_fname = QLOGIC_AH;
        break;
      case 32883:
        dev_fname = QLOGIC_AH;
        break;
      case 32912:
        dev_fname = QLOGIC_AH;
        break;
      case 33136:
        dev_fname = QLOGIC_AHP;
        break;
      case 33168:
        dev_fname = QLOGIC_AHP;
        break;
      case 5638:
        dev_fname = NETXTREME;
        break;
      case 5652:
        dev_fname = NETXTREME;
        break;
      case 5824:
        dev_fname = NETXTREME;
        break;
      case 5825:
        dev_fname = NETXTREME;
        break;
      case 5827:
        dev_fname = NETXTREME;
        break;
      case 5839:
        dev_fname = NETXTREME;
        break;
      case 5846:
        dev_fname = NETXTREME;
        break;
      case 5847:
        dev_fname = NETXTREME;
        break;
      case 5848:
        dev_fname = NETXTREME;
        break;
      case 5849:
        dev_fname = NETXTREME;
        break;
      case 5855:
        dev_fname = NETXTREME;
        break;
      case 5858:
        dev_fname = NETXTREME;
        break;
      case 5859:
        dev_fname = NETXTREME;
        break;
      case 5861:
        dev_fname = NETXTREME;
        break;
      case 5867:
        dev_fname = NETXTREME;
        break;
      case 5869:
        dev_fname = NETXTREME;
        break;
      case 5871:
        dev_fname = NETXTREME;
        break;
      case 5872:
        dev_fname = NETXTREME;
        break;
      case 5873:
        dev_fname = NETXTREME;
        break;
      case 5968:
        dev_fname = NETXTREME;
        break;
      case 5984:
        dev_fname = NETXTREME;
        break;
      case 6169:
        dev_fname = NETXTREME;
        break;
      case 55296:
        dev_fname = NETXTREME;
        break;
      case 55298:
        dev_fname = NETXTREME;
        break;
      case 55300:
        dev_fname = NETXTREME;
        break;
      case 61344:
        dev_fname = EFA;
        break; /* efa0 */
      case 61345:
        dev_fname = EFA;
        break; /* efa1 */
      case 61346:
        dev_fname = EFA;
        break; /* efa2 */
      case 4223:
        dev_fname = ERDMA;
        break;
      case 41506:
        dev_fname = HNS;
        break;
      case 41507:
        dev_fname = HNS;
        break;
      case 41508:
        dev_fname = HNS;
        break;
      case 41509:
        dev_fname = HNS;
        break;
      case 41510:
        dev_fname = HNS;
        break;
      case 41512:
        dev_fname = HNS;
        break;
      case 41519:
        dev_fname = HNS;
        break;
      default:
        dev_fname = UNKNOWN;
    }
  }

  return dev_fname;
}

static void ctx_set_max_inline(
    struct ibv_context *context, struct perftest_parameters *user_param) {
  enum ctx_device current_dev = ib_dev_name(context);

  if (user_param->inline_size == DEF_INLINE) {
    user_param->inline_size = 0;
    if (current_dev == NETXTREME)
      user_param->inline_size = 96;
    else if (current_dev == EFA)
      user_param->inline_size = 32;
    else if (current_dev == QLOGIC_E4)
      user_param->inline_size = 128;
    else if (current_dev == ERDMA)
      user_param->inline_size = 96;
    else if (current_dev == HNS)
      user_param->inline_size = 32;
    else if (current_dev == INTEL_GEN1)
      user_param->inline_size = 48;
    else if (current_dev == INTEL_GEN2)
      user_param->inline_size = 101;
  }

  return;
}

static int get_device_max_reads(
    struct ibv_context *context, struct perftest_parameters *user_param) {
  struct ibv_device_attr attr;
  int max_reads = 0;

  if (!max_reads && !ibv_query_device(context, &attr)) {
    // coverity[uninit_use]
    max_reads = attr.max_qp_rd_atom;
  }
  return max_reads;
}

static int ctx_set_out_reads(
    struct ibv_context *context, struct perftest_parameters *user_param) {
  int max_reads = 0;
  int num_user_reads = user_param->out_reads;

  max_reads = get_device_max_reads(context, user_param);

  if (num_user_reads > max_reads) {
    printf(RESULT_LINE);
    fprintf(
        stderr, " Number of outstanding reads is above max = %d\n", max_reads);
    fprintf(stderr, " Changing to that max value\n");
    num_user_reads = max_reads;
  } else if (num_user_reads <= 0) {
    num_user_reads = max_reads;
  }

  return num_user_reads;
}

static int set_link_layer(
    struct ibv_context *context, struct perftest_parameters *params) {
  struct ibv_port_attr port_attr;
  int8_t curr_link = params->link_type;

  if (ibv_query_port(context, params->ib_port, &port_attr)) {
    fprintf(stderr, " Unable to query port %d attributes\n", params->ib_port);
    return FAILURE;
  }

  if (curr_link == LINK_UNSPEC) {
    // coverity[uninit_use]
    params->link_type = port_attr.link_layer;
  }

  if (port_attr.state != IBV_PORT_ACTIVE) {
    fprintf(
        stderr, " Port number %d state is %s\n", params->ib_port,
        portStates[port_attr.state]);
    return FAILURE;
  }

  if (strcmp("Unknown", link_layer_str(params->link_type)) == 0) {
    fprintf(stderr, "Link layer on port %d is Unknown\n", params->ib_port);
    return FAILURE;
  }
  return SUCCESS;
}

static int ctx_chk_pkey_index(struct ibv_context *context, int pkey_idx) {
  int idx = 0;
  struct ibv_device_attr attr;

  if (!ibv_query_device(context, &attr)) {
    // coverity[uninit_use]
    if (pkey_idx > attr.max_pkeys - 1) {
      printf(RESULT_LINE);
      fprintf(
          stderr, " Specified PKey Index, %i, greater than allowed max, %i\n",
          pkey_idx, attr.max_pkeys - 1);
      fprintf(stderr, " Changing to 0\n");
      idx = 0;
    } else
      idx = pkey_idx;
  } else {
    fprintf(stderr, " Unable to validata PKey Index, changing to 0\n");
    idx = 0;
  }

  return idx;
}

int check_link(
    struct ibv_context *context, struct perftest_parameters *user_param) {
  user_param->transport_type = context->device->transport_type;
  if (set_link_layer(context, user_param) == FAILURE) {
    fprintf(stderr, " Couldn't set the link layer\n");
    return FAILURE;
  }

  if (user_param->link_type == IBV_LINK_LAYER_ETHERNET &&
      user_param->gid_index == -1) {
    user_param->gid_index = 0;
  }

  /* Compute Max inline size with pre found statistics values */
  ctx_set_max_inline(context, user_param);

  user_param->out_reads = ctx_set_out_reads(context, user_param);

  if (user_param->pkey_index > 0)
    user_param->pkey_index =
        ctx_chk_pkey_index(context, user_param->pkey_index);

  return SUCCESS;
}

static int cycles_compare(const void *aptr, const void *bptr) {
  const cycles_t *a = (cycles_t *)aptr;
  const cycles_t *b = (cycles_t *)bptr;
  if (*a < *b) return -1;
  if (*a > *b) return 1;

  return 0;
}

static inline cycles_t get_median(int n, cycles_t *delta) {
  // const cycles_t *delta = (cycles_t *)delta;
  if ((n - 1) % 2)
    return (delta[n / 2] + delta[n / 2 - 1]) / 2;
  else
    return delta[n / 2];
}

#define LAT_MEASURE_TAIL (2)
void print_report_lat(struct perftest_parameters *user_param) {
  int i;
  int rtt_factor;
  double cycles_to_units, cycles_rtt_quotient;
  cycles_t median;
  cycles_t *delta = NULL;
  const char *units;
  double latency, stdev, average_sum = 0, average, stdev_sum = 0;
  int iters_99, iters_99_9;
  int measure_cnt;

  measure_cnt = user_param->iters - 1;
  rtt_factor = 1;
  ALLOCATE(delta, cycles_t, measure_cnt);

  cycles_to_units = get_cpu_mhz(user_param->cpu_freq_f);
  units = "usec";

  for (i = 0; i < measure_cnt; ++i) {
    delta[i] = user_param->tposted[i + 1] - user_param->tposted[i];
  }
  cycles_rtt_quotient = cycles_to_units * rtt_factor;

  qsort(delta, measure_cnt, sizeof *delta, cycles_compare);
  measure_cnt = measure_cnt - LAT_MEASURE_TAIL;
  median = get_median(measure_cnt, delta);

  /* calcualte average sum on sorted array*/
  for (i = 0; i < measure_cnt; ++i)
    average_sum += (delta[i] / cycles_rtt_quotient);

  average = average_sum / measure_cnt;

  /* Calculate stdev by variance*/
  for (i = 0; i < measure_cnt; ++i) {
    int temp_var = average - (delta[i] / cycles_rtt_quotient);
    int pow_var = pow(temp_var, 2);
    stdev_sum += pow_var;
  }

  latency = median / cycles_rtt_quotient;
  stdev = sqrt(stdev_sum / measure_cnt);
  iters_99 = ceil((measure_cnt) * 0.99);
  iters_99_9 = ceil((measure_cnt) * 0.999);

  printf("%lf\n", average);

  free(delta);
}

#endif
