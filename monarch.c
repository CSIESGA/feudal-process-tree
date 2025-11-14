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
        left -= n;
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

// 在 Revolt/Conquer 後刪除 direct child 記錄
static void children_remove_by_pid(pid_t pid, int* childNum) {
    ChildNode** pp = &children_head;
    while (*pp) {
        if ((*pp)->L.pid == pid) {
            ChildNode* r = *pp;
            *pp = r->next;
            close(r->L.read_fd);
            close(r->L.write_fd);
            free(r);
            (*childNum)--;
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
    } else if (isalpha(p[0]) && p[1] == '-' && strncmp(p, "r-Rev-", 6) != 0 && strncmp(p, "r-Rep-", 6) != 0) {
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
// Subtree 組合：合併孩子回傳的子樹字串成多層
//-------------------------------------

// 把各 child 的 subtree 字串（以 '*' 分層）合併成父的 subtree
// parent 第 0 層 = 自己；第 1 層 = 各 child 的第 0 層合併；以此類推
// 這裡為了簡潔，用簡單的分割陣列（MAX_SUBTREE_LAYER=7, 每層用一個暫存字串拼接）
static void merge_subtrees_and_print(const char *self_info, char childSubtrees[][MAX_SUBTREE_LEN], int childN, int* Val) {
    // 把每個 child 的 subtree 分層拆解
    // 層號：0..6
    char layers[ MAX_SUBTREE_LAYER ][ MAX_SUBTREE_LEN ];
    for (int i = 0; i < MAX_SUBTREE_LAYER; i++) 
        layers[i][0]='\0';

    // 第 0 層先放自己
    strncpy(layers[0], self_info, MAX_SUBTREE_LEN);

    // 計算總和：先把自己 fief 納入
    int total = 0;
    {
        char name[MAX_LORD_NAME_LEN]; int v = 0;
        split_lord_info(self_info, name, &v);
        total += v;
    }

    // for 每個孩子：把其各層 append 到父的 +1 層（第0層會加到父第1層）
    for (int c = 0; c < childN; ++c) {
        // 先算孩子整棵的各層
        char tmp[ MAX_SUBTREE_LEN ];
        strncpy(tmp, childSubtrees[c], MAX_SUBTREE_LEN);
        // 子樹總和：也可在另一通道回，但我們這裡直接解析
        // 解析所有 <name_val>，逐一累加
        {
            char copySum[ MAX_SUBTREE_LEN ];
            strncpy(copySum, childSubtrees[c], MAX_SUBTREE_LEN);
            // 把 '*' 換成空白方便逐個 token
            for (char* q = copySum; *q; ++q) 
                if (*q == '*') 
                    *q = ' ';
            char* save = NULL;
            char* tok = strtok_r(copySum, " ", &save);
            while (tok) {
                int v = 0; char nmm[MAX_LORD_NAME_LEN];
                split_lord_info(tok, nmm, &v);
                total += v;
                tok = strtok_r(NULL, " ", &save);
            }
        }

        // 分層合併
        int layerIdx = 1;
        char* save = NULL;
        char* seg = strtok_r(tmp, "*", &save); // 第 0 層（孩子自己）
        while (seg) {
            // append 到對應父層：layers[layerIdx]
            if (layers[layerIdx][0] != '\0') {
                strncat(layers[layerIdx], " ", MAX_SUBTREE_LEN - strlen(layers[layerIdx]));
            }
            strncat(layers[layerIdx], seg, MAX_SUBTREE_LEN - strlen(layers[layerIdx]));
            ++layerIdx;
            seg = strtok_r(NULL, "*", &save);
            if (layerIdx >= MAX_SUBTREE_LAYER) 
                break;
        }
    }

    // 依層列印（空層不印）
    for (int i = 0; i < MAX_SUBTREE_LAYER; i++) {
        if (layers[i][0] == '\0') 
            break;
        fprintf(stdout, "%s\n", layers[i]);
    }
    fprintf(stdout, "%d\n", total);
    *Val = total;
}

//-------------------------------------
// FIFO tools
//-------------------------------------

static int make_fifo_if_needed(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            // 存在但不是 FIFO，刪掉重建
            unlink(path);
        } else {
            return 0; // already ok
        }
    }
    if (mkfifo(path, 0666) < 0) {
        if (errno != EEXIST) ERR_EXIT("mkfifo");
    }
    return 0;
}

//-------------------------------------
// 主程式（每個行程都跑同一份 main）
//-------------------------------------

int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IONBF, 0); // prevent buffered I/O, equivalent to fflush() after each stdout, study this as you may need to do it for other nodes against their parents.
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
            // root：從 stdin 讀到 '\n'
            if (!fgets(line, sizeof(line), stdin)) {
                break; // EOF => 結束程式(或離開主迴圈)
            }
            int L = strlen(line);
            // 去掉尾端 '\n'
            if (L > 0 && line[L-1] == '\n') 
                line[L-1] = '\0';
            if (line[0] == '\0') 
                continue; // 空行就略過
        } else {
            // child：從 FD=3 讀到 '\0'
            fully_read_str(PARENT_READ_FD, line, MAX_FULL_CMD_LEN);
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
                    if(p2c[0] == 3 || p2c[0] == 4 || p2c[1] == 3 || p2c[1] == 4){
                        int temp1 = fcntl(p2c[0], F_DUPFD, 5);
                        int temp2 = fcntl(p2c[1], F_DUPFD, 5);
                        close(p2c[0]); close(p2c[1]);
                        p2c[0] = temp1; p2c[1] = temp2;
                    }
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
        
        // -----------------------------
        // (2) REPORT 與 r-Rep-Report
        // -----------------------------

        else if (strcmp(P->cmd, "Report") == 0) {
            // 兩種情境：
            //  - 原始 Report <X>
            //  - r-Rep-Report <childName> 來自父親要求蒐子樹字串
            bool is_recursive = (strncmp(P->prefix, "r-Rep-", 6) == 0);

            char target[MAX_LORD_NAME_LEN]; 
            strncpy(target, P->arg1, MAX_LORD_NAME_LEN);

            if (strcmp(me->name, target) == 0) {
                // 我就是要報告的根
                // 收集每個直屬子女的 subtree 字串
                char self_info[MAX_LORD_INFO_LEN];
                join_lord_info(me->name, me->fief_size, self_info);

                char childTrees[ MAX_CHILDREN_NUM ][ MAX_SUBTREE_LEN ];
                int childNum = 0;

                for (ChildNode *ch = children_head; ch; ch = ch->next) {
                    // 要求 child 回傳 subtree
                    // 發 r-Rep-Report <childName>
                    char cmd[MAX_FULL_CMD_LEN];
                    snprintf(cmd, sizeof(cmd), "r-Rep-Report %s", ch->L.name);
                    fully_write_str(ch->L.write_fd, cmd);

                    // child 會回一整串 subtree（以 '\0' 結尾），長度上限 MAX_SUBTREE_LEN
                    fully_read_str(ch->L.read_fd, childTrees[childNum], MAX_SUBTREE_LEN);
                    childNum++;
                }

                if (!is_recursive) {
                    // 原始 Report：印層序 + 總和
                    int Val;
                    merge_subtrees_and_print(self_info, childTrees, childNum, &Val);
                    if (!is_WHY(me->name)){ 
                        char status[] = "f";
                        fully_write_str(reply_write_fd, status);
                    }
                    continue;
                }

                // 回覆父親用：把整棵子樹字串回傳（即使是原始 Report，也要把字串回給上一層，以便祖先合併或知道「找到」）
                // 組字串：self 第0層 + 每個 childSubtree 的對應層
                // 這裡重用 merge，但我們需要「不印，只產字串」：
                // 簡便起見：直接按規格合成字串：
                char out[MAX_SUBTREE_LEN];
                // 先放第0層 self
                strncpy(out, self_info, MAX_SUBTREE_LEN);
                // 把 child 的第0層併成第1層
                // 先抽取 child 第0層們
                char layer1[MAX_SUBTREE_LEN]; layer1[0]='\0';
                for (int iCh = 0; iCh < childNum; iCh++){
                    char buf[MAX_SUBTREE_LEN]; strncpy(buf, childTrees[iCh], MAX_SUBTREE_LEN);
                    // 第0層是第一段（遇到 '*' 前）
                    char* star = strchr(buf, '*');
                    if (star) 
                        *star = '\0';
                    if (layer1[0] != '\0')
                        strncat(layer1, " ", MAX_SUBTREE_LEN - strlen(layer1));
                    strncat(layer1, buf, MAX_SUBTREE_LEN - strlen(layer1));
                }
                if (layer1[0] != '\0') {
                    strncat(out, "*", MAX_SUBTREE_LEN - strlen(out));
                    strncat(out, layer1, MAX_SUBTREE_LEN - strlen(out));
                }
                // 更深層（1..6）
                for (int depth = 1; depth < MAX_SUBTREE_LAYER - 1; ++depth) {
                    char layer[MAX_SUBTREE_LEN]; layer[0]='\0';
                    for (int iCh = 0; iCh < childNum; iCh++){
                        // 取 childTrees[iCh] 的 第 depth 層
                        char tmp[MAX_SUBTREE_LEN]; strncpy(tmp, childTrees[iCh], MAX_SUBTREE_LEN);
                        int cur = 0;
                        char* save = NULL;
                        char* seg = strtok_r(tmp, "*", &save);
                        while (seg) {
                            if (cur == depth) {
                                if (layer[0] != '\0')
                                    strncat(layer, " ", MAX_SUBTREE_LEN - strlen(layer));
                                strncat(layer, seg, MAX_SUBTREE_LEN - strlen(layer));
                                break;
                            }
                            cur++;
                            seg = strtok_r(NULL, "*", &save);
                        }
                    }
                    if (layer[0] != '\0') {
                        strncat(out, "*", MAX_SUBTREE_LEN - strlen(out));
                        strncat(out, layer, MAX_SUBTREE_LEN - strlen(out));
                    } else 
                        break;
                }

                // 回傳：如果有父親（即我不是 WHY 或這是遞迴呼叫），就把 out 寫回去
                // if (!is_WHY(me->name) || is_recursive) {
                //     // 注意：即使 root 也可能在 r-Rep- 的鏈上作中繼（這裡統一回）
                //     fully_write_str(reply_write_fd, out);
                // }
                fully_write_str(reply_write_fd, out);
                // 若是原始 Report，已印完；繼續等下一指令
                continue;
            }

            // 我不是目標：往子女傳，蒐狀態，若任何子回有子樹字串，就把字串原封回傳上一層（對遞迴情境）
            bool found = false;
            // char received[MAX_SUBTREE_LEN]; received[0] = '\0';
            for (ChildNode *ch = children_head; ch; ch = ch->next) {
                fully_write_str(ch->L.write_fd, line);
                // 對 Report 的遞迴呼叫，child 會回 subtree 字串（若不是目標分支可能回空？）
                // 簡化：若 child 子樹沒有目標，child 直接回一個一字元狀態 'n'（我們在 child 那邊設計如此）
                // 這裡容忍兩種：收到 1 byte status 或一整個 subtree 字串。
                // 為了穩妥，我們先 peek 一下：規格裡沒提供 peek；因此統一規則：
                // -> 子若沒找到，回 'n\0'（MAX_STATUS_LEN）
                // -> 子若找到，回 subtree（以 '\0' 結尾且長度 >= 3）
                char first[2]; fully_read_str(ch->L.read_fd, first, 2);
                if (first[0] == 'n' && first[1] == '\0') {
                    continue;
                } else if (first[0] == 'f' && first[1] == '\0') {
                    found = true;
                    if (!is_WHY(me->name)){
                        char status[] = "f";
                        fully_write_str(reply_write_fd, status);
                    }
                    break;
                    // // 這是 subtree 的第一兩個 byte，我們需要把剩下的也讀進來：
                    // received[0]=first[0]; received[1]=first[1];
                    // // 之後把剩下資料讀到 '\0'
                    // size_t k=2;
                    // while (k < sizeof(received)) {
                    //     char c;
                    //     ssize_t n=read(ch->L.read_fd, &c, 1);
                    //     if (n<0){ if (errno==EINTR) continue; ERR_EXIT("read subtree"); }
                    //     if (n==0){ break; }
                    //     received[k++] = c;
                    //     if (c=='\0') break;
                    // }
                    // found=true;
                    // break;
                }
            }

            if(is_WHY(me->name) && !found){
                print_fail_report(target);
                continue;
            }
                
            
            if (!found){
                char status[] = "n";
                fully_write_str(reply_write_fd, status);
                continue;
            }
            // if (!found) {
            //     if (is_WHY(me->name) && !is_recursive) {
            //         // 原始 Report 並且找不到
            //         print_fail_report(target);
            //     }
            //     // 回上一層（若有）：'n'
            //     if (!is_WHY(me->name)) {
            //         char st[2]={'n','\0'};
            //         fully_write_str(reply_write_fd, st);
            //     }
            // } else {
            //     // 把 subtree 傳回上一層
            //     if (!is_WHY(me->name)) {
            //         fully_write_str(reply_write_fd, received);
            //     } else {
            //         // 我是 WHY 但不是目標（理論上不會走到，因為 Report 目標可能不是 WHY）；保守不印。
            //     }
            // }
        }

        // -----------------------------
        // (3) EXCHANGE（必用 FIFO）
        // -----------------------------

        else if (strcmp(P->cmd, "Exchange") == 0) {
            // Root 先建立 FIFO；非 root 直接依據兩名字決定自己的角色（A或B或非目標）
            char A[MAX_LORD_NAME_LEN], B[MAX_LORD_NAME_LEN];
            strncpy(A, P->arg1, MAX_LORD_NAME_LEN);
            strncpy(B, P->arg2, MAX_LORD_NAME_LEN);

            char fifoAB[64], fifoBA[64];
            snprintf(fifoAB, sizeof(fifoAB), "/tmp/%s2%s.fifo", A, B);
            snprintf(fifoBA, sizeof(fifoBA), "/tmp/%s2%s.fifo", B, A);

            bool existA = false, existB = false;

            if (is_WHY(me->name)) {
                make_fifo_if_needed(fifoAB);
                make_fifo_if_needed(fifoBA);
            }

            // 判斷自己是否目標
            bool i_am_A = (strcmp(me->name, A) == 0);
            bool i_am_B = (strcmp(me->name, B) == 0);
            // printf("success check 1");

            if (i_am_A || i_am_B) {
                if (!is_WHY(me->name) && i_am_A) {
                    char st[2]={'a','\0'}; 
                    fully_write_str(reply_write_fd, st);
                } else if (!is_WHY(me->name) && i_am_B) {
                    char st[2]={'b','\0'}; 
                    fully_write_str(reply_write_fd, st);
                                  }
                // 目標節點：開 FIFO，寫出自己的 fief，讀入對方的
                // A 寫 A2B，讀 B2A；B 則相反
                const char* write_out = i_am_A ? fifoAB : fifoBA;
                const char* read_in  = i_am_A ? fifoBA : fifoAB;

                // 先以 O_WRONLY 開 outF 可能阻塞（對端尚未 O_RDONLY）
                // 因為兩端都會同時 open，我們可以先 open 讀端避免死等：
                int read_in_fd, write_out_fd;
                if (i_am_A) {
                    read_in_fd = open(read_in,  O_RDONLY); if (read_in_fd < 0) ERR_EXIT("open read_in");
                    write_out_fd = open(write_out, O_WRONLY); if (write_out_fd < 0) ERR_EXIT("open write_out");
                } else {
                    write_out_fd = open(write_out, O_WRONLY); if (write_out_fd < 0) ERR_EXIT("open write_out");
                    read_in_fd = open(read_in,  O_RDONLY); if (read_in_fd < 0) ERR_EXIT("open read_in");
                }
                
                // 寫自己的 fief
                char my_fief[FIEF_SIZE_LEN]; int2fief(me->fief_size, my_fief);
                fully_write_str(write_out_fd, my_fief);
                // 讀對方的 fief
                char your_fief[FIEF_SIZE_LEN]; 
                fully_read_str(read_in_fd, your_fief, FIEF_SIZE_LEN);
                int your_Val = atoi(your_fief);

                // 交換：把自己的改成對方的
                me->fief_size = your_Val;

                close(read_in_fd); close(write_out_fd);

                // 回覆父：'a'
                
                // 上傳存在性給 WHY（在廣播搜集流程中實現）
            } else {
                // 我不是目標：把指令往孩子廣播，收集兩側存在性與成功狀態
                bool haveA = false, haveB = false;
                for (ChildNode* ch = children_head; ch; ch = ch->next) {
                    fully_write_str(ch->L.write_fd, line);
                    // 子若是 A 或 B 會回 'a'（完成交換動作）
                    // 若不是目標但找到某目標存在，可能回 'b' or 'c' 之類；我們這裡簡化：回 'n' 不存在／'a' 有處理
                    char st[2]; fully_read_str(ch->L.read_fd, st, 2);
                    // if (st[0] == 'a') {
                    //     // 有處理成功的一支
                    //     // 這裡無從得知是 A 還是 B，但不重要；繼續等另一支
                    //     // 我們會把這狀態向上回傳 'a' 表示某支成功
                    //     if (!okA) okA=1; else okB=1;
                    // } else if (st[0]=='b') haveA=1;
                    // else if (st[0]=='c') haveB=1;
                    if (st[0] == 'a') {
                        haveA = true;
                    } else if (st[0] == 'b') {
                        haveB = true;
                    } else if (st[0] == 'c'){
                        haveA = true;
                        haveB = true;
                    }
                    // 其他視為該分支無
                }

                // WHY 統一印訊息：因為只有 WHY 會知道兩邊都完成與否
                // if (!is_WHY(me.name)) {
                //     char st[2]={'n','\0'};
                //     fully_write(reply_write_fd, st, 2);
                // }
                if (!is_WHY(me->name)) {
                    if (haveA && haveB) {
                        char st[2] = {'c','\0'};
                        fully_write_str(reply_write_fd, st);
                    } else if (haveA) {
                        char st[2] = {'a','\0'};
                        fully_write_str(reply_write_fd, st);
                    } else if (haveB) {
                        char st[2] = {'b','\0'};
                        fully_write_str(reply_write_fd, st);
                    }
                }

                if (is_WHY(me->name)) {
                    if (haveA && haveB) {
                        print_success_exchange(A, B);
                    } else if (haveA) {
                        print_fail_target_1(B);
                    } else if (haveB) {
                        print_fail_target_0(A);
                    } else {
                        print_fail_target_both();
                    } 
                    // unlink(fifoAB); unlink(fifoBA);
                }
            }

            // if (is_WHY(me.name)) {
            //     // WHY 這裡簡化：再從孩子收集「存在性」不如在前序廣播時由孩子回 'b'/'c'；
            //     // 為了清楚，我們做一輪「存在性確認」：
            //     bool seenA=false, seenB=false, okCnt=0;

            //     // 自己可能是 A/B 嗎？規格保證不會（A/B 非 WHY）
            //     (void)okCnt;

            //     // 從直屬孩子再來一次 ping（更簡易方法：在第一次廣播就收狀態；為篇幅保守，這裡略化）
            //     // 這裡我們直接用 children_find_by_name 快查：如果 A/B 是直屬就算存在
            //     if (children_find_by_name(A)) seenA=true;
            //     if (children_find_by_name(B)) seenB=true;

            //     // 若直屬未命中，仍可能在更深處；這需要在前面的廣播就蒐集 'b'/'c'。
            //     // 為了把訊息確實印出：我們保守地依據是否最終兩端的交換回覆成功來判斷有無（簡易版）：
            //     // ——實作簡化：只要兩端有任何一端成功執行交換步驟，就視為存在。
            //     // 最穩：我們實務上在廣播時讓每個分支都把「是否含 A / 是否含 B」回一個字元上來，但上面為節省篇幅做了'n'/'a'。
            //     // 這裡退一步：若兩端都沒回到 ANY 'a'，我們判定兩者皆不存在；若只回到一個 'a'，則另一個不存在。
            //     // 因課規測資保證 A/B不同且非WHY，我們仍需把訊息印對。
            //     // ——為了確保正確，這裡直接用 FIFO 打開測法：若某 FIFO open 失敗通常代表對側沒 open，但這不可靠。
            //     // 結論：**建議你實際寫 code 時，把存在性回傳做好（'b'=含A，'c'=含B）。** 下方印訊息保持標準格式。

            //     // 這裡直接印「成功交換」：在我們的 above 分支中，只有目標自己會回 'a'；WHY 並未真正統計。
            //     // 為了交付一版可理解的範本，我們此處按規格流程：先清理 FIFO，再不印多餘東西（或依需求調整）。
            //     unlink(fifoAB); unlink(fifoBA);
            //     // 真正的「存在性收集 + 印三種錯誤」在完整版建議你把 'b'/'c' 狀態做完（篇幅關係此處簡化）。
            // }
        }

        // -----------------------------
        // (4) REVOLT 與 r-Rev-*
        // -----------------------------
        
        else if (strcmp(P->cmd, "Revolt") == 0) {
            char target[MAX_LORD_NAME_LEN]; strncpy(target, P->arg1, MAX_LORD_NAME_LEN);
            bool is_recursive = (strncmp(P->prefix, "r-Rev-", 6) == 0);
            int Value_sum;
 
            if (strcmp(me->name, target) == 0) {
                if (!is_recursive) {
                    char self_info[MAX_LORD_INFO_LEN];
                    join_lord_info(me->name, me->fief_size, self_info);

                    char childTrees[ MAX_CHILDREN_NUM ][ MAX_SUBTREE_LEN ];
                    int childNum = 0;

                    for (ChildNode *ch = children_head; ch; ch = ch->next) {
                        // 要求 child 回傳 subtree
                        // 發 r-Rep-Report <childName>
                        char cmd[MAX_FULL_CMD_LEN];
                        snprintf(cmd, sizeof(cmd), "r-Rep-Report %s", ch->L.name);
                        fully_write_str(ch->L.write_fd, cmd);

                        // child 會回一整串 subtree（以 '\0' 結尾），長度上限 MAX_SUBTREE_LEN
                        fully_read_str(ch->L.read_fd, childTrees[childNum], MAX_SUBTREE_LEN);
                        childNum++;
                    }

                    merge_subtrees_and_print(self_info, childTrees, childNum, &Value_sum);
                }
            
                // 再遞迴關閉整棵子樹：對每個 child 發 r-Rev-Revolt <childName>
                for (ChildNode *ch = children_head; ch; ch = ch->next) {
                    char cmd[MAX_FULL_CMD_LEN];
                    snprintf(cmd, sizeof(cmd), "r-Rev-Revolt %s", ch->L.name);
                    fully_write_str(ch->L.write_fd, cmd);

                    // 回收 child
                    int status = 0; waitpid(ch->L.pid, &status, 0);
                    close(ch->L.read_fd); close(ch->L.write_fd);
                }
                // 清空 children list
                while (children_head) {
                    ChildNode* tmp = children_head->next;
                    free(children_head);
                    children_head = tmp;
                }
                children_count = 0;

                if (!is_recursive) {
                    // 把 total 回傳給父親（若有）
                    if (!is_WHY(me->name)) {
                        char val[5];
                        int2fief(Value_sum, val);
                        fully_write_str(reply_write_fd, val);
                        close(reply_write_fd);
                        close(cmd_read_fd);
                        // 自己退出
                        _exit(0);
                    } else {
                        // Revolt WHY：印結束訊息並結束
                        print_finish();
                        close(cmd_read_fd);
                        _exit(0);
                    }
                } else {
                    close(reply_write_fd);
                    close(cmd_read_fd);
                    _exit(0);
                }
            }                

            // 我不是目標：往子女傳
            bool found = false;
            for (ChildNode *ch = children_head; ch; ch = ch->next) {
                fully_write_str(ch->L.write_fd, line);
                // child 若不是目標，會繼續傳；如果 target 在其子樹，他最終會把 sum4 回上來（遞迴路徑）
                // 如果 target 不在其子樹，他回 'n'（STATUS）
                // 這裡我們容忍兩種：先讀兩 bytes看是不是 'n\0'，不是的話就當作 sum4，其餘用 exact 讀
                if (strcmp(target, ch->L.name) == 0) {
                    char add_value[5];
                    fully_read_str(ch->L.read_fd, add_value, 5);
                    int value = atoi(add_value);
                    me->fief_size += value;
                    int status = 0;
                    waitpid(ch->L.pid, &status, 0);
                    // close(ch->L.read_fd); close(ch->L.write_fd);
                    children_remove_by_pid(ch->L.pid, &children_count); // 從 list 移除
                    // if (children_head) {
                    //     printf("Childhead is not NULL\n");
                    //     // printf(children_head->L.name);
                    //     printf("%d\n", children_count);
                    // } else {
                    //     printf("Null\n");
                    // }
                    // printf("Direct find child\n");
                    found = true;
                    break;
                }
                char first2[2];
                fully_read_str(ch->L.read_fd, first2, 2);
                // printf("I reaceived my child messeage!\n");
                // printf("%s\n", first2);
                if (first2[0] == 'n' && first2[1] == '\0') {
                    continue; // 該分支沒有
                } else if (first2[0] == 'f' && first2[1] == '\0') {
                    found = true;
                    break;
                }
            }

            if (!found) {
                if (is_WHY(me->name)) {
                    // 找不到
                    print_fail_not_exist(target);
                }
                // 回報 'n'
                if (!is_WHY(me->name)) {
                    char st[2] = {'n', '\0'}; 
                    fully_write_str(reply_write_fd, st);
                }
            } else {
                // 我收到 sum 並完成加回，如果我也被 r-Rev- 要結束，會在上層處理
                if (!is_WHY(me->name)) {
                    // 回 sum4 或 'a'？這裡回 'a' 告知上一層已完成一支
                    char st[2] = {'f','\0'};
                    // printf("I write to parent!\n");
                    fully_write_str(reply_write_fd, st);
                    // printf("I already write to parent!\n");
                }
            }
        }
        
    //     // -----------------------------
    //     // (5) CONQUER（必用 FIFO）
    //     // -----------------------------

    //     else if (strcmp(P.cmd,"Conquer")==0) {
    //         // 完整版流程很長，受篇幅限制，這裡給出骨架與關鍵操作
    //         char A[MAX_LORD_NAME_LEN], B[MAX_LORD_NAME_LEN];
    //         strncpy(A, P.arg1, MAX_LORD_NAME_LEN);
    //         strncpy(B, P.arg2, MAX_LORD_NAME_LEN);

    //         char fifoAB[64], fifoBA[64];
    //         snprintf(fifoAB, sizeof(fifoAB), "%s2%s.fifo", A, B);
    //         snprintf(fifoBA, sizeof(fifoBA), "%s2%s.fifo", B, A);

    //         if (is_WHY(me.name)) {
    //             make_fifo_if_needed(fifoAB);
    //             make_fifo_if_needed(fifoBA);
    //         }

    //         bool iAmA = (strcmp(me.name, A)==0);
    //         bool iAmB = (strcmp(me.name, B)==0);

    //         // 每個目標先取得自己的「子樹總和」
    //         if (iAmA || iAmB) {
    //             // 蒐集自己子樹總和（不印）
    //             int total = me.fief_size;
    //             // 讓每個 child 回報其子樹總和（用 r-Rep-Report 或專門「r-Rep-Conquer」都可）
    //             for (ChildNode *ch=children_head; ch; ch=ch->next) {
    //                 char cmd[MAX_FULL_CMD_LEN];
    //                 snprintf(cmd, sizeof(cmd), "r-Rep-Report %s", ch->L.name);
    //                 fully_write(ch->L.write_fd, cmd, strlen(cmd)+1);
    //                 // child 回 subtree 字串，解析加總（同 Report）
    //                 char subtree[MAX_SUBTREE_LEN]; fully_read_cstr(ch->L.read_fd, subtree, MAX_SUBTREE_LEN);
    //                 // 把 '*' 換空白後逐個 token 加總
    //                 for (char *q=subtree; *q; ++q) if (*q=='*') *q=' ';
    //                 char *save=NULL; char *tok=strtok_r(subtree," ",&save);
    //                 while (tok) {
    //                     int v=0; char nm[MAX_LORD_NAME_LEN];
    //                     split_lord_info(tok, nm, &v);
    //                     total += v;
    //                     tok = strtok_r(NULL, " ", &save);
    //                 }
    //             }

    //             // 與對方透過 FIFO 交換「總和」
    //             const char *outF = iAmA ? fifoAB : fifoBA;
    //             const char *inF  = iAmA ? fifoBA : fifoAB;

    //             int in_fd  = open(inF,  O_RDONLY); if (in_fd<0) ERR_EXIT("open inF");
    //             int out_fd = open(outF, O_WRONLY); if (out_fd<0) ERR_EXIT("open outF");

    //             char my4[FIEF_SIZE_LEN]; i2fief4(total, my4);
    //             fully_write(out_fd, my4, FIEF_SIZE_LEN);

    //             char peer4[FIEF_SIZE_LEN]; fully_read_exact(in_fd, peer4, FIEF_SIZE_LEN);
    //             int peerTotal = atoi(peer4);

    //             close(in_fd); close(out_fd);

    //             // 比較大小：自己輸→執行 Revolt（本地）
    //             if (total < peerTotal) {
    //                 // 敗者：直接觸發本地 Revolt（與 (4) 分支同樣步驟）
    //                 // 這裡簡寫：向父親回傳 sum4，由父做分配（父會等）
    //                 // ...（篇幅限制，請參照 Revolt 的收尾步驟）
    //             } else if (total > peerTotal) {
    //                 // 勝者：等待對方終止；父會做分配
    //             } else {
    //                 // 平手：雙方父親各加回自己的 total；印 equal
    //             }

    //             if (!is_WHY(me.name)) {
    //                 char st[2]={'a','\0'}; fully_write(reply_write_fd, st, 2);
    //             }
    //         } else {
    //             // 非目標：往子女傳，再收合狀態（略）
    //             for (ChildNode *ch=children_head; ch; ch=ch->next) {
    //                 fully_write(ch->L.write_fd, line, strlen(line)+1);
    //                 char st[2]; fully_read_exact(ch->L.read_fd, st, 2);
    //                 (void)st;
    //             }
    //             if (!is_WHY(me.name)) {
    //                 char st[2]={'n','\0'}; fully_write(reply_write_fd, st, 2);
    //             }
    //         }

    //         if (is_WHY(me.name)) {
    //             // 印勝負 or equal，並刪 FIFO（略；完整版你可在前序廣播就收齊兩邊的總和與存在性）
    //             unlink(fifoAB); unlink(fifoBA);
    //         }
    //     }
    //     // -----------------------------
    //     // (6) VISIT（LCA，Bonus）
    //     // -----------------------------
    //     else if (strcmp(P.cmd,"Visit")==0) {
    //         char A[MAX_LORD_NAME_LEN], B[MAX_LORD_NAME_LEN];
    //         strncpy(A, P.arg1, MAX_LORD_NAME_LEN);
    //         strncpy(B, P.arg2, MAX_LORD_NAME_LEN);

    //         // 回傳狀態：'a' 無、'b' 含 A、'c' 含 B、'd' 同時含（或 LCA）
    //         // 略化：若自己是 A 或 B，先標記
    //         int haveA = (strcmp(me.name, A)==0);
    //         int haveB = (strcmp(me.name, B)==0);

    //         char lcaName[MAX_LORD_NAME_LEN]="";

    //         // 收子女狀態
    //         int cntA=haveA, cntB=haveB;
    //         for (ChildNode *ch=children_head; ch; ch=ch->next) {
    //             fully_write(ch->L.write_fd, line, strlen(line)+1);
    //             char st[2]; fully_read_exact(ch->L.read_fd, st, 2);
    //             if (st[0]=='b') cntA++;
    //             else if (st[0]=='c') cntB++;
    //             else if (st[0]=='d') {
    //                 // child 已判定 LCA 在其子樹，讓他再回傳名字
    //                 char nm[MAX_LORD_NAME_LEN]; fully_read_cstr(ch->L.read_fd, nm, MAX_LORD_NAME_LEN);
    //                 strncpy(lcaName, nm, MAX_LORD_NAME_LEN);
    //                 // 把 'd' 往上回
    //                 if (!is_WHY(me.name)) {
    //                     char d[2]={'d','\0'}; fully_write(reply_write_fd, d, 2);
    //                     fully_write(reply_write_fd, lcaName, strlen(lcaName)+1);
    //                 } else {
    //                     print_success_visit(A, B, lcaName);
    //                 }
    //                 goto VISIT_NEXT_CMD;
    //             }
    //         }

    //         if (cntA && cntB) {
    //             // 我是 LCA
    //             if (!is_WHY(me.name)) {
    //                 char d[2]={'d','\0'}; fully_write(reply_write_fd, d, 2);
    //                 fully_write(reply_write_fd, me.name, strlen(me.name)+1);
    //             } else {
    //                 print_success_visit(A,B,me.name);
    //             }
    //         } else if (cntA) {
    //             if (!is_WHY(me.name)) { char b[2]={'b','\0'}; fully_write(reply_write_fd, b, 2); }
    //         } else if (cntB) {
    //             if (!is_WHY(me.name)) { char c[2]={'c','\0'}; fully_write(reply_write_fd, c, 2); }
    //         } else {
    //             if (is_WHY(me.name)) {
    //                 // 兩者都不存在（或其中之一不存在），題目要求印三種錯誤；此處略化，可依實作在前序廣播時統計存在性後印。
    //             } else {
    //                 char a[2]={'a','\0'}; fully_write(reply_write_fd, a, 2);
    //             }
    //         }
    //     VISIT_NEXT_CMD:
    //         ;
    //     }
    //     else {
    //         // 不會出現；若出現，直接列印錯誤便於偵錯
    //         fprintf(stderr, "%s received error input : %s\n", me.name, line);
    //     }
    //      free(P);
    } // while

    // END_PROGRAM:
    // //Final print, please leave this in, it may bepart of the test case output
    // if (is_WHY(me->name)) {
    //     print_finish();
    // }
    // return 0;
}