#define PARENT_READ_FD 3
#define PARENT_WRITE_FD 4
#define MAX_LORD_NAME_LEN 16
#define MAX_CMD_LEN 15 
#define MAX_FULL_CMD_LEN 49
#define MAX_LORD_INFO_LEN 21
#define MAX_STATUS_LEN 2
#define FIEF_SIZE_DIGIT 5
#define MAX_SUBTREE_LEN 630
#define MAX_FIFO_INFO_LEN 5
#define MAX_LAYER 6
#define MAX_CHILDREN_NUM 8
#define MAX_SUBTREE_NODE 30
#define EXCHANGE_FIFO "Exchange.fifo"
#define CONQUER_FIFO "Conquer.fifo"
#define VISIT_FIFO "Visit.fifo"
#include <sys/types.h>

typedef struct {
    pid_t pid;
    int read_fd;
    int write_fd;
    char name[MAX_LORD_NAME_LEN];
    int fief_size;
} Lord;
