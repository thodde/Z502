#ifndef LIST
#define LIST
#include "my_globals.h"

// Define what a linked list and its nodes are
typedef struct {
    PCB* data;
    struct Node* next;
} Node, *LinkedList;

// function prototypes
LinkedList create_list();
int add_to_list( LinkedList, PCB*);
PCB* remove_from_list(LinkedList, INT32);
int get_length(LinkedList l);
PCB* search_for_pid(LinkedList l, INT32 pid);
PCB* search_for_name(LinkedList l, char* name);
PCB* search_by_parent(LinkedList l, INT32 pid);
LinkedList build_ready_queue(LinkedList l);
void free_ready_queue(LinkedList l);
LinkedList order_by_priority(LinkedList l);

#endif
