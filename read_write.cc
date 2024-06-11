#include "communication.h"

void rdma_write(rdma_context *ctx) {
  std::string tst = "hello_zhixiangxiang";
  uint32_t len = tst.size();

  char *ans = (char *)ctx->buf[0];
  for (int i = 0; i < len; i++) {
    ans[i] = tst[i];
  }
  for (int i = 0; i < len; i++) {
    printf("%d", ans[i]);
  }
  printf("\n");
  run_write(ctx, len);
}

void rdma_read(rdma_context *ctx) {
  std::string tst = "hello_zhixiangxiang";
  uint32_t len = tst.size();

  run_read(ctx, len);

  char *ans = (char *)ctx->buf[0];
  printf("ans %d\n", len);
  for (int i = 0; i < len; i++) {
    printf("%d", ans[i]);
  }
  printf("\n");
}

int main(int argc, char *argv[]) {
  printf("This is the concise version of perftest.\n");

  rdma_parameter user_param;
  int ret_parser = user_param.parser(argv, argc);
  if (ret_parser) {
    printf("Failed to parser parameter.\n");
    exit(0);
  }
  //   user_param.verb = READ;
  user_param.verb = WRITE;

  force_dependecies(&user_param);

  rdma_comm user_comm;

  rdma_context ctx;
  struct message_context *my_dest = NULL;
  struct message_context *rem_dest = NULL;

  memset(&ctx, 0, sizeof(rdma_context));

  memset(&user_comm, 0, sizeof(rdma_comm));

  struct ibv_device *ib_dev = ctx_find_dev(&user_param.ib_devname);
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

  // exchange_versions(&user_comm, &user_param);

  if (check_mtu(ctx.context, &user_param, &user_comm)) {
    fprintf(stderr, " Couldn't get context for the device\n");
    dealloc_comm_struct(&user_comm, &user_param);
    return 0;
  }

  MAIN_ALLOC(
      my_dest, struct message_context, user_param.num_of_qps, free_rdma_params);
  memset(my_dest, 0, sizeof(struct message_context) * user_param.num_of_qps);
  MAIN_ALLOC(
      rem_dest, struct message_context, user_param.num_of_qps, free_my_dest);
  memset(rem_dest, 0, sizeof(struct message_context) * user_param.num_of_qps);

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

  // user_param.print_para();
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

  // write api
  write_init(&ctx, &user_param, rem_dest);
  rdma_write(&ctx);

  // read api
  read_init(&ctx, &user_param, rem_dest);
  rdma_read(&ctx);

  //   printf(RESULT_LINE);
  //   printf("%s", RESULT_FMT_LAT);
  //   printf(RESULT_EXT);

  // user_param.print_para();
  // print_ctx(&ctx);

  //   for (int i = 1; i < 24; ++i) {
  //     user_param.size = (uint64_t)1 << i;
  //     if (user_param.verb == READ) {
  //       if (run_iter_lat(&ctx, &user_param)) {
  //         goto free_mem;
  //       }
  //     } else if (user_param.verb == WRITE) {
  //       if (run_iter_lat_write(&ctx, &user_param)) {
  //         goto free_mem;
  //       }
  //     }
  //     print_report_lat(&user_param);
  //   }

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
