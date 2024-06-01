#include "comm.h"

int main(int argc, char *argv[]) {
  printf("This is the concise version of perftest.\n");
  int ret_parser, i = 0, rc, error = 1;
  struct report_options report;
  struct pingpong_context ctx;
  struct ibv_device *ib_dev;
  struct perftest_parameters user_param;
  struct pingpong_dest *my_dest = NULL;
  struct pingpong_dest *rem_dest = NULL;
  struct perftest_comm user_comm;
  // int rdma_cm_flow_destroyed = 0;

  memset(&ctx, 0, sizeof(struct pingpong_context));
  memset(&user_param, 0, sizeof(struct perftest_parameters));
  memset(&user_comm, 0, sizeof(struct perftest_comm));

  ret_parser = parser(&user_param, argv, argc);

  ib_dev = ctx_find_dev(&user_param.ib_devname);
  if (!ib_dev) {
    fprintf(stderr, " Unable to find the Infiniband/RoCE device\n");
    return 0;
  }

  ctx.context = ctx_open_device(ib_dev, &user_param);
  if (!ctx.context) {
    fprintf(stderr, " Couldn't get context for the device\n");
    return 0;
  }

  if (check_link(ctx.context, &user_param)) {
    fprintf(stderr, " Couldn't get context for the device\n");
    return 0;
  }

  if (create_comm_struct(&user_comm, &user_param)) {
    fprintf(stderr, " Unable to create RDMA_CM resources\n");
    return 0;
  }

  if (user_param.machine == SERVER) {
    printf("\n************************************\n");
    printf("* Waiting for client to connect... *\n");
    printf("************************************\n");
  }

  if (establish_connection(&user_comm)) {
    fprintf(stderr, " Unable to init the socket connection\n");
    return 0;
  }

  exchange_versions(&user_comm, &user_param);

  if (check_mtu(ctx.context, &user_param, &user_comm)) {
    fprintf(stderr, " Couldn't get context for the device\n");
    dealloc_comm_struct(&user_comm, &user_param);
    return 0;
  }

  MAIN_ALLOC(
      my_dest, struct pingpong_dest, user_param.num_of_qps, free_rdma_params);
  memset(my_dest, 0, sizeof(struct pingpong_dest) * user_param.num_of_qps);
  MAIN_ALLOC(
      rem_dest, struct pingpong_dest, user_param.num_of_qps, free_my_dest);
  memset(rem_dest, 0, sizeof(struct pingpong_dest) * user_param.num_of_qps);

  if (alloc_ctx(&ctx, &user_param)) {
    fprintf(stderr, "Couldn't allocate context\n");
    return 0;
  }

  if (ctx_init(&ctx, &user_param)) {
    fprintf(stderr, " Couldn't create IB resources\n");
    dealloc_ctx(&ctx, &user_param);
    goto free_mem;
  }

  /* Set up the Connection. */
  if (set_up_connection(&ctx, &user_param, my_dest)) {
    fprintf(stderr, " Unable to set up socket connection\n");
    exit(0);
  }

  if (ctx_hand_shake(&user_comm, my_dest, rem_dest)) {
    fprintf(stderr, "Failed to exchange data between server and clients\n");
    exit(0);
  }

  for (i = 0; i < user_param.num_of_qps; i++) {
    /* shaking hands and gather the other side info. */
    if (ctx_hand_shake(&user_comm, &my_dest[i], &rem_dest[i])) {
      fprintf(stderr, "Failed to exchange data between server and clients\n");
      exit(0);
    }
  }

  // if (ctx_check_gid_compatibility(&my_dest[0], &rem_dest[0])) {
  //   fprintf(stderr, "\n Found Incompatibility issue with GID types.\n");
  //   fprintf(stderr, " Please Try to use a different IP version.\n\n");
  //   goto destroy_context;
  // }

  if (ctx_connect(&ctx, rem_dest, &user_param, my_dest)) {
    fprintf(stderr, " Unable to Connect the HCA's through the link\n");
    exit(-1);
  }

  user_comm.rdma_params->side = REMOTE;

  for (i = 0; i < user_param.num_of_qps; i++) {
    if (ctx_hand_shake(&user_comm, &my_dest[i], &rem_dest[i])) {
      fprintf(stderr, " Failed to exchange data between server and clients\n");
      exit(-1);
    }
  }

  if (ctx_hand_shake(&user_comm, my_dest, rem_dest)) {
    fprintf(stderr, "Failed to exchange data between server and clients\n");
    exit(-1);
  }

  user_param.print_para();
  // ctx.print_para();

  if (user_param.machine == SERVER) {
    if (ctx_close_connection(&user_comm, my_dest, rem_dest)) {
      fprintf(stderr, "Failed to close connection between server and client\n");
      goto free_mem;
    }

    free(rem_dest);
    free(my_dest);
    free(user_param.ib_devname);
    free(user_comm.rdma_params);
    return SUCCESS;
  }

  printf(RESULT_LINE);
  printf("%s", RESULT_FMT_LAT);
  printf(RESULT_EXT);

  ctx_set_send_wqes(&ctx, &user_param, rem_dest);

  // for (i = 1; i < 24; ++i) {
  for (i = 12; i < 14; ++i) {
    user_param.size = (uint64_t)1 << i;
    if (run_iter_lat(&ctx, &user_param)) {
      error = 17;
      goto free_mem;
    }
    print_report_lat(&user_param);
  }

  if (ctx_close_connection(&user_comm, my_dest, rem_dest)) {
    fprintf(stderr, "Failed to close connection between server and client\n");
    goto free_mem;
  }
  free(rem_dest);
  free(my_dest);
  free(user_param.ib_devname);
  free(user_comm.rdma_params);
  return SUCCESS;

free_rdma_params:
  free(user_comm.rdma_params);
free_my_dest:
  free(my_dest);
free_mem:
  free(rem_dest);
}
