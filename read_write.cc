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

void rdma_init(
    rdma_context &ctx, rdma_parameter &user_param, rdma_comm &user_comm,
    message_context *&my_dest, message_context *&rem_dest) {
  user_param.verb = WRITE;

  force_dependecies(&user_param);
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

int main(int argc, char *argv[]) {
  printf("This is the concise version of perftest.\n");

  rdma_parameter user_param;
  int ret_parser = user_param.parser(argv, argc);
  if (ret_parser) {
    printf("Failed to parser parameter.\n");
    exit(0);
  }

  rdma_comm user_comm;
  rdma_context ctx;
  struct message_context *my_dest = NULL;
  struct message_context *rem_dest = NULL;

  rdma_init(ctx, user_param, user_comm, my_dest, rem_dest);

  // write api
  write_init(&ctx, &user_param, rem_dest);
  rdma_write(&ctx);

  // read api
  read_init(&ctx, &user_param, rem_dest);
  rdma_read(&ctx);

  if (ctx_close_connection(&user_comm, my_dest, rem_dest)) {
    fprintf(stderr, "Failed to close connection between server and client\n");
    free(rem_dest);
    return FAILURE;
  }
  free(rem_dest);
  free(my_dest);
  free(user_param.ib_devname);
  free(user_comm.rdma_params);
  return SUCCESS;
}
