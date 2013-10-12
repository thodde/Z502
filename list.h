#ifndef LIST
#define LIST
#include "my_globals.h"

typedef struct {
    PCB* data;
    struct Node* next;
} Node, *LinkedList;

LinkedList create_list();
int add_to_list( LinkedList, PCB*);

#endif
