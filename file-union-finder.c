#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
extern int errno;

struct operating_arguments {
  const char *left_file;
  const char *right_file;
  const char *out_file;
};
struct operating_arguments op_args;

// The maximum line length here is pretty small.
// If we needed a larger length, we'd need to grow this,
// but this one is fine small for now.
#define MAX_LINE_LEN 256
struct context {
  // Our file handles...
  FILE *left_fh;
  FILE *right_fh;
  FILE *out_fh;

  // These are our line buffers. We're going to rotate through them
  // to avoid creating and destroying memory, but we're going to use
  // pointers to reduce the complexity of the code (irony, right?).
  char left_a[MAX_LINE_LEN];
  char left_b[MAX_LINE_LEN];
  char right_a[MAX_LINE_LEN];
  char right_b[MAX_LINE_LEN];

  // In order to detect when the files are out of sort order, we need
  // to keep track of the last line.
  char *prev_left_line;
  char *prev_right_line;

  // These are the lines we're going to actually test.
  char *left_line;
  char *right_line;
};
#define INIT_CHAR_BUFF(b) memset((void*)b, 0, MAX_LINE_LEN)

#define PRINT_OP_ARGS() \
  printf("%s U %s -> %s\n",op_args.left_file,op_args.right_file,op_args.out_file)

void usage();
struct operating_arguments *args(int argc, const char *argv[]);
void bail(const char *msg, struct context *ctx);
void detect_sort_problems(struct context *ctx);
FILE* open_file(const char *fname, const char *mode);
void open_context(struct context *ctx);
void close_context(struct context *ctx);
char advance_side(struct context *ctx, char LorR);

int main(int argc, const char *argv[]) {
  struct context ctx;
  args(argc, argv);
  PRINT_OP_ARGS();
  open_context(&ctx);

  char active_files = 2;
  while (active_files == 2) {
    switch(strncmp(ctx.left_line, ctx.right_line, MAX_LINE_LEN)) {
      case -1:
        active_files = advance_side(&ctx, 'L');
      case 1:
        active_files += advance_side(&ctx, 'R');
      default: // Default is zero
        fprintf(ctx.out_fh, "%s\n", ctx.left_line);
        active_files = advance_side(&ctx, 'L');
        active_files += advance_side(&ctx, 'R');
        break;
    };
  }

  close_context(&ctx);
  printf("Done.\n");
  return EXIT_SUCCESS;
}


void usage() {
  printf("Usage: fufinder LEFT_FILE RIGHT_FILE OUTPUT_FILE\n");
  exit(EXIT_FAILURE);
}

struct operating_arguments *args(int argc, const char *argv[]) {
  if (argc != 4) {
    usage();
  }

  op_args.left_file = argv[1];
  op_args.right_file = argv[2];
  op_args.out_file = argv[3];

  return &op_args;
}

void bail(const char *msg, struct context *ctx) {
    fprintf(stderr, "Error: %s", msg);
    fclose(ctx->left_fh);
    fclose(ctx->right_fh);
    fclose(ctx->out_fh);
    exit(EXIT_FAILURE);
}
void detect_sort_problems(struct context *ctx) {
  if (ctx->left_line[0] != '\0' && strcmp(ctx->prev_left_line, ctx->left_line) > 0) {
    fprintf(stderr, "prev:%s -- curr:%s\n", ctx->prev_left_line, ctx->left_line);
    bail("ERROR: Your left file is out of sort order. This program requires all files be in sort order.\n", ctx);
    return;
  }
  if (ctx->right_line[0] != '\0' && strcmp(ctx->prev_right_line, ctx->right_line) > 0) {
    fprintf(stderr, "prev:%s -- curr:%s\n", ctx->prev_right_line, ctx->right_line);
    bail("ERROR: Your right file is out of sort order. This program requires all files be in sort order.\n", ctx);
    return;
  }

  return;
}

FILE* open_file(const char *fname, const char *mode) {
  // Should probably lock files...
  FILE *fh = fopen(fname, mode);
  if (!fh) fprintf(stderr, "Failed to open file \"%s\": %s\n", fname, strerror(errno));
  return fh;
}
void open_context(struct context *ctx) {
  ctx->left_fh = open_file(op_args.left_file, "r");
  ctx->right_fh = open_file(op_args.right_file, "r");
  ctx->out_fh = open_file(op_args.out_file, "w");
  INIT_CHAR_BUFF(ctx->left_a);
  INIT_CHAR_BUFF(ctx->left_b);
  INIT_CHAR_BUFF(ctx->right_a);
  INIT_CHAR_BUFF(ctx->right_b);
  ctx->prev_left_line = ctx->left_a;
  ctx->left_line = ctx->left_b;
  ctx->prev_right_line = ctx->right_a;
  ctx->right_line = ctx->right_b;

  if (!advance_side(ctx, 'L')) {
    bail("Left file is empty.\n", ctx);
  }
  if (!advance_side(ctx, 'R')) {
    bail("Right file is empty.\n", ctx);
  }
  
  return;
}
void close_context(struct context *ctx) {
  fclose(ctx->left_fh);
  fclose(ctx->right_fh);
  fflush(ctx->out_fh); // Just in case...
  fclose(ctx->out_fh);

  return;
}

char advance_side(struct context *ctx, char LorR) {
  char *ptr = NULL;
  char **line = NULL;
  char **prev = NULL;
  FILE **fh = NULL;
  const char *fname = NULL;
  int bytes = 0;
  size_t len = MAX_LINE_LEN-1;

  // What we're doing is the same regardless of side, so we're
  // just setting up our pointers to play nice with which ever
  // side we're advancing.
  switch (LorR) {
    case 'L':
      line = &(ctx->left_line);
      prev = &(ctx->prev_left_line);
      fh = &(ctx->left_fh);
      fname = op_args.left_file;
      break;
    case 'R':
      line = &(ctx->right_line);
      prev = &(ctx->prev_right_line);
      fh = &(ctx->right_fh);
      fname = op_args.right_file;
      break;
    default:
      bail("Stupid error in a call to `advance_side()` (bad LorR)\n", ctx);
      break;
  }

  // Rotate the buffers if needed.
  if (strnlen(*line, len) > 0) {
    ptr = *prev;
    *prev = *line;
    *line = ptr;
  }

  // Now read the new line.
  char *rs = fgets(*line, MAX_LINE_LEN-1, *fh);
  if (!rs && ferror(*fh)) {
    fprintf(stderr, "Failed to read a line from  file \"%s\": %s\n", fname, strerror(errno));
    bail("Error occurred.\n", ctx);
  }
  // Kill the newline.
  else if (rs) {
    len = strnlen(*line, MAX_LINE_LEN-1)-1;
    (*line)[len] = '\0';

    // Just in case someone gave us a bad file...
    detect_sort_problems(ctx);
  }
  else {
    memset(*line, '\0', MAX_LINE_LEN);
  }

  // Return the opposite of whether the end-of-file
  // has been reached.
  return !feof(*fh);
}
