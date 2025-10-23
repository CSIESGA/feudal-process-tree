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

#include "hw2.h"

#define ERR_EXIT(s) perror(s), exit(errno);

/*
If you need help from TAs,
please remember :
0. Show your efforts
    0.1 Fully understand course materials
    0.2 Read the spec thoroughly, including frequently updated FAQ section
    0.3 Use online resources
    0.4 Ask your friends while avoiding plagiarism, they might be able to understand you better, since the TAs know the solution, 
        they might not understand what you're trying to do as quickly as someone who is also doing this homework.
1. Be respectful
2. The quality of your question will directly impact the value of the response you get.
3. Think about what you want from your question, what is the response you expect to get
4. What do you want the TA to help you with. 
    4.0 Unrelated to Homework (wsl, workstation, systems configuration)
    4.1 Debug
    4.2 Logic evaluation (we may answer doable yes or no, but not always correct or incorrect, as it might be giving out the solution)
    4.3 Spec details inquiry
    4.4 Testcase possibility
5. If the solution to answering your question requires the TA to look at your code, you probably shouldn't ask it.
6. We CANNOT tell you the answer, but we can tell you how your current effort may approach it.
7. If you come with nothing, we cannot help you with anything.
*/

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

/* Remember read and write may not be fully transmitted in HW1?
void fully_write(int write_fd, void *write_buf, int write_len);

void fully_read(int read_fd, void *read_buf, int read_len);

please do above 2 functions to save some time
*/

int main(int argc, char *argv[]) {
    // Hi! Welcome to SP Homework 2, I hope you have fun
    // Basic variables used by every process, you can move these to global variable or delete them as you please.
    char node_info[MAX_LORD_INFO_LEN]; // current process lord_info.
    Lord* lord = malloc(sizeof(Lord));
    FILE* read_fp = NULL;
    // You also need a structure to record the children info, we recommand you implement in linked list. 

    pid_t process_pid = getpid(); // you might need this when using fork()
    if (argc != 2) {
        fprintf(stderr, "Usage: ./monarch [lord_info]\n");
        return 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0); // prevent buffered I/O, equivalent to fflush() after each stdout, study this as you may need to do it for other nodes against their parents.
    
    // Put argument one into lord_info
    strncpy(node_info, argv[1], MAX_LORD_INFO_LEN);
    
    // Start to run the main process (root WHY).
    if(is_WHY(node_info)){
        strncpy(lord->name, node_info,MAX_LORD_NAME_LEN);      // put name into lord structure
        lord->name[3] = '\0';        // in case strcmp messes with you
        read_fp = stdin;             // takes commands from stdin
        lord->fief_size = 10000;     // fief_size is 10000 for why at begining
    }
    // Create child process (other nodes).
    else{
        // extract name and value from info
        // where do you read from?
        // anything else you have to do before you start taking commands?
    }

    //TODO:
    /* You may follow SOP if you wish, but it is not guaranteed to consider every possible outcome

    1. Read from parent/stdin
    2. Determine what the command is (Grant, Report, Exchange, Revolt, Conquer, Visit(bonus)), I recommend using strcmp() and/or char check
    3. Find out who should execute the command (extract information received)
    4. Execute the command or tell the target lords to execute the command
        4.1 Command passing may be required here
    5. After previous command is done, repeat step 1.
    */

    // Hint: Do not stop the loop before receiving the command "Revolt" and the node in the target's subtree.
    // Please keep in mind that every process runs this exact same program, so think of all the possible cases and implement them

    /* pseudo code
    if(Grant){
        create array[2]
        make pipe
        use fork.
            Hint: Remember to fully understand how fork works, what it copies or doesn't
        check if you are parent or child
        as parent or child, think about what you do next.
            Hint: child needs to run this program again
    }
    else if(Report){
        obtain the info of this subtree, what are their info?
        distribute the info into levels 1 to 6 (refer to Additional Specifications: subtree level <= 6)
        use above distribution to print out level by level
            Q: why do above? can you make each process print itself?
            Hint: we can only print line by line, is DFS or BFS better in this case?
    }
    else if(Revolt){
        perform Report
        terminate the entire subtree
            Q1: what resources have to be cleaned up and why?
            Hint: Check pseudo code above
            Q2: what commands needs to be executed? what are their orders to avoid deadlock or infinite blocking?
            A: (tell child to die, reap child, tell parent you're dead, return (die))
        the parent of target node should update the children info structure
    }
    else if(Exchange){
        remember to make fifo
        find the two targets nodes
        collect the existence status when passing command
        decide the time and the order for exchange data in FIFOs
        root should handle the case when targets not exist
    }
    else if(Conquer){
        remember to make fifo
        find the two targets nodes
        collect the existence status when passing command
        perform Report to collect the total fief size
        exchange total fief size
        winner update the total fief size
        loser perform Revolt to kill the subtree
            Note: the parent of loser should keep waiting for the loser node terminate
        root should handle the case when targets not exist
    } else if(Visit){
        finding the LCA of two target nodes
        bonus has no hints :D
    }
    else{
        there's an error, we only have valid commmands in the test cases
        fprintf(stderr, "%s received error input : %s\n", lord->name, full_cmd); // use this to print out what you received
    }
    */

   // Final print, please leave this in, it may bepart of the test case output
    if(is_WHY(lord->name)){
        print_finish();
    }
    return 0;
}