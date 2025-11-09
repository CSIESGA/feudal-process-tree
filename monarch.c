#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>

#include "hw2.h"

#define ERR_EXIT(s) perror(s), exit(errno);

// The root should take different action from others node, you would frequently checking if the node is root.
static char root[MAX_LORD_NAME_LEN] = "WHY";

static inline bool is_WHY(char name[]) {
    return (strcmp(name, root) == 0);
}

// A bunch of prints for you
void print_direct_grant(char *lord_name) {
    fprintf(stdout, "WHY has granted %s by himself.\n", lord_name);
}

void print_indirect_grant(char *parent_lord_name, char *child_lord_name) {
    fprintf(stdout, "%s has granted %s.\n", parent_lord_name, child_lord_name);
}

void print_fail_not_exist(char *lord_A) {
    fprintf(stdout, "%s does not exist in the kingdom.\n", lord_A);
}

void print_fail_grant_fief_size(char *parent_lord_name, char* child_lord_name) {
    fprintf(stdout, "%s does not have enough fiefdom to grant %s.\n", parent_lord_name, child_lord_name);
}

void print_fail_report(char *lord_name){
    fprintf(stdout, "WHY does not get any report from %s.\n", lord_name);
}

void print_success_exchange(char *lord_A, char *lord_B) {
    fprintf(stdout, "%s and %s has exchanged their fief.\n", lord_A, lord_B);
}

void print_success_conquer(char *lord_A, char *lord_B, int fief_size) {
    fprintf(stdout, "%s conquers %s, grabbing %d land.\n", lord_A, lord_B, fief_size);
}

void print_equal_conquer() {
    printf("Both side suffer, all lords died in the war.\n");
}

void print_success_visit(char *lord_A, char *lord_B, char *lord_C) {
    fprintf(stdout, "%s is the LCA for %s and %s.\n", lord_C, lord_A, lord_B);
}

void print_fail_target_0(char *lord_A) {
    fprintf(stdout, "First lord %s does not exist in the kingdom.\n", lord_A);
}

void print_fail_target_1(char *lord_B) {
    fprintf(stdout, "Second lord %s does not exist in the kingdom.\n", lord_B);
}

void print_fail_target_both() {
    fprintf(stdout, "Both lords do not exist in the kingdom.\n");
}

void print_finish(){
    fprintf(stdout, "Congradulations! You've finished these tasks!\n");
}

/* Terminate child pseudo code
void clean_child(){
    close(child read_fd);
    close(child write_fd);
    call wait() or waitpid() to reap child; // this is blocking
}
*/

// Remember read and write may not be fully transmitted in HW1?
// static void fully_write_str(int write_fd, const char *s) {
//     // s 必須是以 '\0' 結尾的有效 C 字串
//     int left = strlen(s) + 1;  // +1 把 '\0' 也送出去
//     const char *p = s;
//     while (left > 0) {
//         int n = write(write_fd, p, left);
//         p += n;
//         left -= n;
//     }
// }

static void fully_write_str(int write_fd, const char *s) {
    int left = strlen(s) + 1;     // 包含 '\0'
    const char *p = s;
    while (left > 0) {
        int n = write(write_fd, p, left);
        if (n < 0) {
            if (errno == EINTR) continue;
            return;          // 例如 EPIPE：對端沒有讀者
        }
        if (n == 0) { errno = EPIPE; return; }
        p += n;
        left -= (size_t)n;
    }
    return;
}

// static void fully_read_str(int read_fd, char *buf) {
//     int i = 0;
//     while (1) {
//         char c;
//         int n = read(read_fd, &c, 1);
//         // if (n == 0) {        // EOF
//         //     if (i == 0) return 0;   // 還沒讀到任何東西就 EOF
//         //     // 已讀到部分資料但沒有 '\0'，我們補 '\0' 以避免上層爆，並回傳長度（不含真正的結尾）
//         //     buf[i] = '\0';
//         //     return (ssize_t)(i + 1);
//         // }
//         // 有讀到 1 byte
//         buf[i++] = c;
//         if (c == '\0' || c == '\n')
//             return;
//     }
// }

static void fully_read_str(int read_fd, char *buf, int maxlen) {
    int i = 0;
    for (;;) {
        char c;
        int n = read(read_fd, &c, 1);
        // write(STDOUT_FILENO, &c, sizeof(c));
        if (n < 0) {                 // read 錯誤
            if (errno == EINTR) continue;
            if (i < maxlen) buf[i] = '\0';
            return;
        }
        if (n == 0) {                // EOF：對端關閉
            if (i < maxlen) buf[i] = '\0';
            return;
        }
        if (i < maxlen) buf[i++] = c;   // 保留結尾位置
        if (c == '\0' || c == '\n'){
            buf[i - 1] = '\0';
            return;
        } // 規格允許 '\0'
    }
}

// 將 int 轉為 4位數字字串（含 '\0'），例如 37 -> "0037"
static void int2fief(int v, char out[FIEF_SIZE_LEN]) {
    snprintf(out, FIEF_SIZE_LEN, "%04d", v);
}

// 由 <name_val> 轉成 name 與 fief int，name 僅小寫 a-z（規格）
// e.g. "pikachu_0600" -> name="pikachu", val=600
static void split_lord_info(const char *info, char name[MAX_LORD_NAME_LEN], int *val) {
    // info 長度 <= MAX_LORD_INFO_LEN，格式 <name>_<dddd>
    const char *us = strchr(info, '_');
    int namelen = (int)(us - info);
    memcpy(name, info, namelen);
    name[namelen] = '\0';
    int v = 0;
    v = (us[1]-'0')*1000 + (us[2]-'0')*100 + (us[3]-'0')*10 + (us[4]-'0');
    *val = v;
}

// 將 name+fief 組成 lord_info 字串（含 '\0'）
static void join_lord_info(const char *name, int val, char out[MAX_LORD_INFO_LEN]) {
    char fv[5];
    int2fief(val, fv);
    snprintf(out, MAX_LORD_INFO_LEN, "%s_%s", name, fv);
}

//-------------------------------------
// Child list：按建立順序管理直接子女
//-------------------------------------

typedef struct ChildNode {
    Lord L;                    // 只用到 pid/read_fd/write_fd/name/fief_size
    struct ChildNode *next;
} ChildNode;

static ChildNode *children_head = NULL;
static int children_count = 0;

static void children_push(pid_t pid, int read_fd, int write_fd, const char *name, int fief) {
    ChildNode *n = (ChildNode*)calloc(1, sizeof(ChildNode));
    n->L.pid = pid; 
    n->L.read_fd = read_fd; 
    n->L.write_fd = write_fd;
    strncpy(n->L.name, name, MAX_LORD_NAME_LEN);
    n->L.fief_size = fief;
    n->next = NULL;
    if (!children_head) 
        children_head = n;
    else {
        ChildNode *p = children_head;
        while (p->next) 
            p = p->next;
        p->next = n;
    }
    children_count++;
}

static ChildNode* children_find_by_name(const char *name) {
    for (ChildNode *p=children_head; p; p=p->next) {
        if (strcmp(p->L.name, name) == 0) 
            return p;
    }
    return NULL;
}

// 在 Revolt/Conquer 後刪除 direct child 記錄
static void children_remove_by_pid(pid_t pid) {
    ChildNode **pp = &children_head;
    while (*pp) {
        if ((*pp)->L.pid == pid) {
            ChildNode *r = *pp;
            *pp = r->next;
            close(r->L.read_fd);
            close(r->L.write_fd);
            free(r);
            children_count--;
            return;
        }
        pp = &((*pp)->next);
    }
}

//-------------------------------------
// 指令解析：支持前綴（狀態、遞迴）
//-------------------------------------
typedef struct {
    char raw[MAX_FULL_CMD_LEN];     // 原始（含 '\0'）
    char status[3];                 // "x-" 一個字母+'-'，或空字串
    char prefix[7];                 // 可能是 "r-Rep-" 或 "r-Rev-"
    char cmd[MAX_CMD_NAME_LEN];     // Grant/Report/Exchange/Revolt/Conquer/Visit
    char arg1[MAX_LORD_NAME_LEN];   // 第一個參數（名字或 WHY）
    char arg2[MAX_LORD_NAME_LEN];   // 第二個參數（名字）
    char info[MAX_LORD_INFO_LEN];   // 可能用在 Grant 的 child_info
    bool has_info;                  // 是否帶 lord_info
    bool has_status;                // 是否帶狀態前綴  
    bool has_prefix;                // 是否有"r-Rep-" 或 "r-Rev-"
} Parsed;

static void trim_newline(char *s) {
    int n = strlen(s);
    if (n && s[n-1] == '\n') 
        s[n-1] = '\0';
}

static void parse_command_line(const char *in, Parsed *out) {
    // memset(out, 0, sizeof(Parsed));
    strncpy(out->raw, in, MAX_FULL_CMD_LEN);
    out->raw[MAX_FULL_CMD_LEN - 1] = '\0';
 
    // 拆 prefix
    const char *p = out->raw;
    if (isalpha(p[0]) && p[1] == '-' && (strncmp(&p[2], "r-Rev-", 6) == 0 || strncmp(&p[2], "r-Rep-", 6) == 0)) {
        // 狀態前綴: "x-"
        out->status[0]=p[0]; 
        out->status[1]='-'; 
        out->status[2]='\0';
        out->has_status = true;
        // "r-Rep-" 或 "r-Rev-"
        strncpy(out->prefix, &p[2], 6);
        out->prefix[6] = '\0';
        out->has_prefix = true;
        p += 8;
    } else if (isalpha(p[0]) && p[1] == '-' && strncmp(&p[2], "r-Rev-", 6) != 0 && strncmp(&p[2], "r-Rep-", 6) != 0) {
        // 狀態前綴: "x-"
        out->status[0]=p[0]; 
        out->status[1]='-'; 
        out->status[2]='\0';
        out->has_status = true;
        p += 2;
    } else if (strncmp(p, "r-Rep-", 6) == 0) {
        strcpy(out->prefix,"r-Rep-");
        out->has_prefix = true;
        p += 6;
    } else if (strncmp(p, "r-Rev-", 6) == 0) {
        strcpy(out->prefix,"r-Rev-");
        out->has_prefix = true;
        p += 6;
    }
    // 現在 p 指向真正的 command 開頭
    // 三類：Grant P C_info / Report P / Exchange A B / Revolt P / Conquer A B / Visit A B
    // 這裡用 sscanf 辨識；Grant 需要 lord_info（小寫名_4位）
    char tmp1[MAX_LORD_NAME_LEN], tmp2[MAX_LORD_NAME_LEN], tmp3[MAX_LORD_INFO_LEN];
    if (sscanf(p, "Grant %s %s", tmp1, tmp3) == 2) {
        strncpy(out->cmd, "Grant", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
        strncpy(out->info, tmp3, MAX_LORD_INFO_LEN);
        out->has_info = true;
    } else if (sscanf(p, "Report %s", tmp1) == 1) {
        strncpy(out->cmd, "Report", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
    } else if (sscanf(p, "Exchange %s %s", tmp1, tmp2) == 2) {
        strncpy(out->cmd, "Exchange", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
        strncpy(out->arg2, tmp2, MAX_LORD_NAME_LEN);
    } else if (sscanf(p, "Revolt %s", tmp1) == 1) {
        strncpy(out->cmd, "Revolt", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
    } else if (sscanf(p, "Conquer %s %s", tmp1, tmp2) == 2) {
        strncpy(out->cmd, "Conquer", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
        strncpy(out->arg2, tmp2, MAX_LORD_NAME_LEN);
    } else if (sscanf(p, "Visit %s %s", tmp1, tmp2) == 2) {
        strncpy(out->cmd, "Visit", MAX_CMD_NAME_LEN);
        strncpy(out->arg1, tmp1, MAX_LORD_NAME_LEN);
        strncpy(out->arg2, tmp2, MAX_LORD_NAME_LEN);
    }
}

//-------------------------------------
// 主程式（每個行程都跑同一份 main）
//-------------------------------------

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0); // prevent buffered I/O, equivalent to fflush() after each stdout, study this as you may need to do it for other nodes against their parents.
    signal(SIGPIPE, SIG_IGN);          // ← 避免被 write() 直接打死
    if (argc != 2) {
        fprintf(stderr, "Usage: ./monarch [lord_info]\n");
        return 0;
    }

    // 初始化本節點資訊
    char node_info[MAX_LORD_INFO_LEN]; // current process lord_info.
    Lord* me = malloc(sizeof(Lord));
    // You also need a structure to record the children info, we recommand you implement in linked list. 

    // pid_t process_pid = getpid(); // you might need this when using fork()
    
    // Put argument one into lord_info
    strncpy(node_info, argv[1], MAX_LORD_INFO_LEN);
    
    if (is_WHY(node_info)) {
        strncpy(me->name, "WHY", MAX_LORD_NAME_LEN);
        me->name[3] = '\0';
        me->fief_size = 10000;
    } else {
        // child：從 argv[1] 萃取 name / fief
        int val = 0; 
        char name[MAX_LORD_NAME_LEN];
        split_lord_info(node_info, name, &val);
        strncpy(me->name, name, MAX_LORD_NAME_LEN);
        me->fief_size = val;
        // 子節點的讀寫：已由 parent 在 exec 前安排
        // parent→child 在 child 端用 FD=3 讀；child→parent 用 FD=4 寫
    }
    // printf("%s %d\n", me->name, me->fief_size);

    // char buffer[20];
    // int p2c[2];
    // pipe(p2c);
    
    // // fully_read_str(p2c[0], buffer, 20);
    // char line1[] = "hello hi\n";
    // fully_write_str(p2c[1], line1);
    // fully_read_str(p2c[0], buffer, 20);
    // write(STDOUT_FILENO, buffer, 20);
    // return 0;
    
    // 讀指令的來源：
    //  從 stdin 讀；其他節點從 FD=3（PARENT_READ_FD）讀
    int cmd_read_fd = is_WHY(me->name) ? STDIN_FILENO : PARENT_READ_FD;
    // 回覆（狀態/子樹/數字）給父母：root 無父母；子用 FD=4
    int reply_write_fd = is_WHY(me->name) ? -1 : PARENT_WRITE_FD;

    // 主迴圈：不斷讀取一行命令（以 '\n' 結尾），或父親傳來的 r-Rep- / r-Rev- 命令
    while (1) {
        // 先宣告共用緩衝
        char line[MAX_FULL_CMD_LEN];  // 放指令（最後會是以 '\0' 結尾的 C-string）

       
        
        // 1) 讀入一條「完整指令字串」
        if (is_WHY(me->name)) {
            if (!fgets(line, sizeof(line), stdin)) {
                break; // EOF => 結束程式(或離開主迴圈)
            }
            int L = strlen(line);
            if (L > 0 && line[L-1] == '\n') 
                line[L-1] = '\0';
            // printf("%s\n", line);
            if (line[0] == '\0') 
                continue; // 空行就略過
            // // root：從 stdin 讀到 '\n'
            // fgets(line, sizeof(line), stdin);
            // // 去掉尾端 '\n'
            // int L = strlen(line);
            // if (L > 0 && line[L-1] == '\n') 
            //     line[L-1] = '\0';
        } else {
            // child：從 FD=3 讀到 '\0'
            // printf("test\n");
            fully_read_str(PARENT_READ_FD, line, MAX_FULL_CMD_LEN);
            // printf("%s\n", line);
        }

        Parsed* P = calloc(1, sizeof(Parsed));
        parse_command_line(line, P);
    
        // -----------------------------
        // (1) GRANT
        // -----------------------------
        
        if (strcmp(P->cmd, "Grant") == 0) {
            // Grant <parent> <child_info>
            char parentName[MAX_LORD_NAME_LEN]; 
            strncpy(parentName, P->arg1, MAX_LORD_NAME_LEN);
            char childName[MAX_LORD_NAME_LEN];
            int childVal = 0;
            split_lord_info(P->info, childName, &childVal);

            bool done=false, found=false, success=false;

            if (strcmp(me->name, parentName) == 0) {
                found = true;
                // 是否有足夠 fief
                if (me->fief_size <= childVal) {
                    // 失敗：不夠fiefsize
                    print_fail_grant_fief_size(parentName, childName);
                    success=false;
                    done=true;
                } else {
                    // 成功且fiefsize足夠
                    // 建立兩條 pipe
                    int p2c[2], c2p[2];
                    if (pipe(p2c)<0) ERR_EXIT("pipe p2c");
                    if (pipe(c2p)<0) ERR_EXIT("pipe c2p");
                    pid_t pid = fork();
                    if (pid < 0) ERR_EXIT("fork");
                    if (pid == 0) {
                        // child
                        // 關閉不用端
                        close(p2c[1]); close(c2p[0]);
                        // 重新導向：父→子讀在 3；子→父寫在 4
                        if (dup2(p2c[0], PARENT_READ_FD) < 0) ERR_EXIT("dup2 p2c[0]->3");
                        if (dup2(c2p[1], PARENT_WRITE_FD) < 0) ERR_EXIT("dup2 c2p[1]->4");
                        // 關掉原本 fd
                        close(p2c[0]); close(c2p[1]);
                        // exec monarch child_info
                        execl("./monarch", "./monarch", P->info, (char*)NULL);
                        ERR_EXIT("execl child");
                    } else {
                        // parent
                        close(p2c[0]); close(c2p[1]);
                        // 更新父本地資源
                        me->fief_size -= childVal;
                        children_push(pid, c2p[0], p2c[1], childName, childVal);
                        if (is_WHY(me->name)) 
                            print_direct_grant(childName);
                        else 
                            print_indirect_grant(me->name, childName);
                        success=true;
                        done=true;
                    }
                }
            }

            if (!done) {
                // 往子女傳（前序）
                for (ChildNode *ch=children_head; ch; ch=ch->next) {
                    // 原封不動傳下（這裡也可以加狀態 prefix，但對 Grant 非必要）
                    fully_write_str(ch->L.write_fd, line);
                    // 等子回覆一個 status：'a' 成功／已處理、'z' 未處理...
                    char st[MAX_STATUS_LEN];
                    fully_read_str(ch->L.read_fd, st, MAX_STATUS_LEN);
                    // printf("read_success");
                    if (st[0] == 'a'){
                        success=true; found=true; 
                        break; 
                    } else if (st[0] == 'f') { 
                        found = true; /* child說目標存在但失敗（錢不夠）*/ 
                        break; 
                    }
                    // 其他狀態代表該分支沒找到，繼續傳下一個孩子
                }
            }

            if (is_WHY(me->name)) {
                if (!found)
                    print_fail_not_exist(parentName);
                // 若 found 但失敗（錢不夠）已在子或自己印過
            }

            // 回報給父
            if (!is_WHY(me->name)) {
                char st[2] = { success ? 'a' : (found ? 'f' : 'n'), '\0' };
                fully_write_str(reply_write_fd, st);
            }
        }

        free(P);
    }


//    Final print, please leave this in, it may bepart of the test case output
//     if(is_WHY(Lord->name)){
//         print_finish();
//     }
//     return 0;
}