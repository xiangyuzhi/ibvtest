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

  len = 10;

  run_read(ctx, len * 4);

  int *ans = (int *)ctx->buf[0];
  printf("ans %d\n", len);
  for (int i = 0; i < len; i++) {
    printf("%d ", ans[i]);
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

  rdma_comm user_comm;
  rdma_context ctx;
  struct message_context *my_dest = NULL;
  struct message_context *rem_dest = NULL;

  com_init(ctx, user_param, user_comm, my_dest, rem_dest);

  // write api
  // write_init(&ctx, &user_param, rem_dest);
  // rdma_write(&ctx);

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
